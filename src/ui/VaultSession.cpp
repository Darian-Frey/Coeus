#include "ui/VaultSession.hpp"

#include "index/IndexService.hpp"
#include "search/EmbeddingService.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/McpLink.hpp"
#include "vault/Roots.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QThread>
#include <QTimer>

namespace coeus {

namespace {

// Open sessions by vault path: a window opening a vault another has open joins it.
QHash<QString, std::weak_ptr<VaultSession>>& sessions()
{
    static QHash<QString, std::weak_ptr<VaultSession>> open;
    return open;
}

} // namespace

std::shared_ptr<VaultSession> VaultSession::open(const Vault& vault, bool* created)
{
    if (std::shared_ptr<VaultSession> existing = sessions().value(vault.rootPath()).lock()) {
        if (created)
            *created = false;
        return existing;
    }
    std::shared_ptr<VaultSession> session(new VaultSession(vault));
    sessions().insert(vault.rootPath(), session);
    if (created)
        *created = true;
    return session;
}

VaultSession::VaultSession(const Vault& vault)
    : m_vault(vault)
{
    // MCP writes (F-033, AV-012): `coeus --mcp` asks before touching a note open
    // in any window and reports each write, which a window offers to undo.
    m_mcpLink = new McpLink(
        [this](const QString& path) {
            MainWindow* window = windowFor(path);
            if (!window)
                return McpLink::Buffer{};
            const Editor* editor = window->editorFor(path);
            if (!editor)
                editor = window->editorFor(QFileInfo(path).canonicalFilePath());
            return McpLink::Buffer{true, editor->isModified(), editor->isModified() ? editor->toPlainText() : QString()};
        },
        [this](const QString& path, const std::optional<QByteArray>& before, const QByteArray& afterHash, const QString& summary) {
            // The window showing the note, else the active one, else the first.
            MainWindow* window = windowFor(path);
            for (MainWindow* candidate : windows())
                if (!window && candidate->isActiveWindow())
                    window = candidate;
            if (!window && !windows().isEmpty())
                window = windows().front();
            if (window)
                window->showWriteToast(path, before, afterHash, summary);
        },
        this);
    m_mcpLink->listen(vault.rootPath());

    m_recentTimer = new QTimer(this);
    m_recentTimer->setSingleShot(true);
    m_recentTimer->setInterval(kRecentSaveDelayMs);
    connect(m_recentTimer, &QTimer::timeout, this, [this] {
        if (auto saved = saveRecent(); !saved)
            emit warning(tr("Recent notes not saved: %1").arg(saved.error()));
    });
    m_recent = RecentNotes::load(vault.rootPath());
    m_bookmarks = Bookmarks::load(vault.rootPath());

    loadRoots();
    start();
}

VaultSession::~VaultSession()
{
    stop();
    (void)saveRecent();
    m_mcpLink->close();
    // Only this session's entry: a newer session of the vault may already be listed.
    auto& open = sessions();
    if (auto it = open.find(m_vault.rootPath()); it != open.end() && it->expired())
        open.erase(it);
}

void VaultSession::loadRoots()
{
    m_linkedRoots.clear();
    m_explorerRoots = {ExplorerRoot{m_vault.name(), m_vault.rootPath(), true}};
    for (const RootConfig& root : Roots::load(m_vault.rootPath()).roots) {
        const auto opened = Vault::open(root.path);
        m_explorerRoots.append(ExplorerRoot{root.name, root.path, opened.has_value()});
        if (opened)
            m_linkedRoots.push_back({root.name, *opened});
    }
}

void VaultSession::reloadRoots()
{
    stop();
    loadRoots();
    start();
    emit rootsChanged();
    emit restarted();
}

void VaultSession::start()
{
    const QString vaultPath = m_vault.rootPath();
    m_indexState.reset();
    m_embeddingState.reset();
    m_embeddingUnavailable.reset();

    m_indexThread = new QThread(this);
    m_indexThread->setObjectName(QStringLiteral("coeus-index"));
    m_index = new IndexService; // no parent: it moves to the worker thread
    m_index->moveToThread(m_indexThread);
    connect(m_indexThread, &QThread::finished, m_index, &QObject::deleteLater);
    connect(m_index, &IndexService::opened, this,
            [this](int notes, const QStringList& problems) { m_indexState = std::pair{notes, problems}; });
    connect(m_index, &IndexService::indexChanged, this, [this](int notes) {
        if (m_indexState)
            m_indexState->first = notes;
    });

    // The embedding worker (F-018): its own low-priority thread, woken by every index change.
    m_embedThread = new QThread(this);
    m_embedThread->setObjectName(QStringLiteral("coeus-embed"));
    m_embedding = new EmbeddingService; // no parent: it moves to the worker thread
    m_embedding->moveToThread(m_embedThread);
    connect(m_embedThread, &QThread::finished, m_embedding, &QObject::deleteLater);
    connect(m_embedding, &EmbeddingService::progress, this,
            [this](int embedded, int total) { m_embeddingState = std::pair{embedded, total}; });
    connect(m_embedding, &EmbeddingService::unavailable, this, [this](const QString& why) { m_embeddingUnavailable = why; });
    const auto wakeEmbedding = [this, vaultPath](bool reopen) {
        QMetaObject::invokeMethod(m_embedding, [service = m_embedding, vaultPath, reopen] {
            if (reopen)
                service->open(vaultPath);
            service->wake();
        }, Qt::QueuedConnection);
    };
    connect(m_index, &IndexService::opened, this, [wakeEmbedding] { wakeEmbedding(true); });
    connect(m_index, &IndexService::indexChanged, this, [wakeEmbedding] { wakeEmbedding(false); });
    m_embedThread->start(QThread::LowPriority);

    m_indexThread->start();
    QMetaObject::invokeMethod(m_index, [service = m_index, vaultPath] { service->open(vaultPath); }, Qt::QueuedConnection);
}

void VaultSession::stop()
{
    if (m_embedThread) {
        m_embedThread->quit(); // returns after the batch in hand, at most about half a second
        m_embedThread->wait();
        delete m_embedThread;
        m_embedThread = nullptr;
        m_embedding = nullptr; // deleted on its own thread as it finished
    }
    if (m_indexThread) {
        m_indexThread->quit();
        m_indexThread->wait();
        delete m_indexThread;
        m_indexThread = nullptr;
        m_index = nullptr;
    }
}

void VaultSession::attach(MainWindow* window)
{
    if (!windows().contains(window))
        m_windows.append(window);
}

void VaultSession::detach(MainWindow* window)
{
    m_windows.removeAll(window);
    m_windows.removeAll(nullptr);
}

QList<MainWindow*> VaultSession::windows() const
{
    QList<MainWindow*> live;
    for (const QPointer<MainWindow>& window : m_windows)
        if (window)
            live.append(window);
    return live;
}

MainWindow* VaultSession::windowFor(const QString& absolutePath) const
{
    const QString canonical = QFileInfo(absolutePath).canonicalFilePath();
    for (MainWindow* window : windows())
        if (window->editorFor(absolutePath) || (!canonical.isEmpty() && window->editorFor(canonical)))
            return window;
    return nullptr;
}

std::expected<void, QString> VaultSession::setBookmarks(const QList<Bookmark>& bookmarks)
{
    m_bookmarks = bookmarks;
    auto saved = Bookmarks::save(m_vault.rootPath(), m_bookmarks);
    emit bookmarksChanged();
    return saved;
}

void VaultSession::touchRecent(const RecentNotes::Note& note)
{
    if (!m_recent.isEmpty() && m_recent.front() == note)
        return;
    m_recent = RecentNotes::touched(m_recent, note);
    m_recentPending = true;
    m_recentTimer->start();
}

std::expected<void, QString> VaultSession::saveRecent()
{
    // A flag, not the timer: a single-shot timer is no longer active in its own timeout (BUG-012).
    if (!m_recentPending)
        return {};
    m_recentPending = false;
    m_recentTimer->stop();
    return RecentNotes::save(m_vault.rootPath(), m_recent);
}

} // namespace coeus
