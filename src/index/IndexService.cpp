#include "index/IndexService.hpp"

#include "index/IndexRoots.hpp"
#include "vault/VaultConfig.hpp"

#include "vault/GitIgnore.hpp"
#include "vault/TempSweeper.hpp"
#include "vault/VaultWatcher.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace coeus {

IndexService::IndexService(QObject* parent)
    : QObject(parent)
{
}

IndexService::~IndexService() = default;

QString IndexService::indexPath(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus/index.db");
}

std::optional<Indexer::IgnoreFn> IndexService::ignoreFor(int rootId) const
{
    // Always a filter, even outside a repository: nested ones are found as
    // folders are checked.
    const std::shared_ptr<GitIgnore> ignore = m_ignores.value(rootId);
    if (!ignore)
        return std::nullopt;
    return Indexer::IgnoreFn([ignore](const QString& path, bool isDir) { return ignore->isIgnored(path, isDir); });
}

void IndexService::open(const QString& vaultPath)
{
    m_vaultPath = vaultPath;
    qDeleteAll(m_watchers);
    m_watchers.clear();
    m_ignores.clear();
    m_roots.clear();
    m_indexer.reset();
    m_store.reset();

    if (!QDir().mkpath(vaultPath + QStringLiteral("/.coeus"))) {
        emit failed(tr("Cannot create %1/.coeus").arg(vaultPath));
        return;
    }
    auto store = IndexStore::open(indexPath(vaultPath));
    if (!store) {
        emit failed(store.error());
        return;
    }
    m_store.emplace(std::move(*store));
    m_indexer = std::make_unique<Indexer>(*m_store);
    // [semantic] chunk_tokens and chunk_overlap; a change re-chunks every note on the next scan.
    m_indexer->setChunking(int(VaultConfig::integer(vaultPath, "semantic", "chunk_tokens").value_or(Chunker::kDefaultTokens)),
                           int(VaultConfig::integer(vaultPath, "semantic", "chunk_overlap").value_or(Chunker::kDefaultOverlap)));

    // Root 0 is the vault; linked roots keep their id by name (SPEC.md §Linked roots).
    const auto reconciled = IndexRoots::reconcile(*m_store, vaultPath);
    if (!reconciled) {
        emit failed(reconciled.error());
        return;
    }
    const QStringList problems = reconciled->problems;
    for (const IndexRoots::Root& root : reconciled->roots)
        m_roots.insert(root.info.id, {root.info, root.present});

    // Watch before scanning, so nothing written during the scan is missed.
    QList<int> ids = m_roots.keys();
    std::sort(ids.begin(), ids.end());
    for (const int id : ids) {
        const RootState& root = m_roots.value(id);
        if (!root.present)
            continue; // AV-010: an absent root is never scanned, so never emptied
        m_ignores.insert(id, std::make_shared<GitIgnore>(root.info.path));
        if (id != 0)
            TempSweeper::sweep(root.info.path); // the vault itself is swept by MainWindow
        startWatching(root);
    }
    for (const int id : std::as_const(ids)) {
        if (m_roots.value(id).present)
            rescanRoot(id);
    }

    const auto count = m_store->noteCount();
    emit opened(count.value_or(0), problems);
}

void IndexService::startWatching(const RootState& root)
{
    const auto ignore = ignoreFor(root.info.id);
    auto* watcher = new VaultWatcher(root.info.id, root.info.path, ignore.value_or(VaultWatcher::IgnoreFn()), this);
    connect(watcher, &VaultWatcher::filesChanged, this, &IndexService::applyChanges);
    connect(watcher, &VaultWatcher::rescanNeeded, this, &IndexService::rescanRoot);
    connect(watcher, &VaultWatcher::watchLimitReached, this,
            [this](int, const QString& message) { emit warning(message); });
    m_watchers.insert(root.info.id, watcher);
    watcher->start();
}

void IndexService::applyChanges(int rootId, const QStringList& paths)
{
    if (!m_indexer || !m_roots.contains(rootId))
        return;
    const auto ignore = ignoreFor(rootId);
    const RootInfo& root = m_roots.value(rootId).info;
    if (auto result = m_indexer->update(root, paths, ignore.value_or(Indexer::IgnoreFn())); !result) {
        emit failed(result.error());
        return;
    }
    QStringList absolute;
    absolute.reserve(paths.size());
    for (const QString& path : paths)
        absolute.append(root.path + QLatin1Char('/') + path);
    emit filesChanged(absolute);
    reportCount();
}

void IndexService::rescanRoot(int rootId)
{
    if (!m_indexer || !m_roots.contains(rootId))
        return;
    const RootState& root = m_roots.value(rootId);
    if (!root.present)
        return;

    // A .gitignore may have changed: reload the rules for the scan and the watcher.
    m_ignores.insert(rootId, std::make_shared<GitIgnore>(root.info.path));
    const auto ignore = ignoreFor(rootId);
    if (VaultWatcher* watcher = m_watchers.value(rootId))
        watcher->setIgnore(ignore.value_or(VaultWatcher::IgnoreFn()));

    emit busy(tr("Indexing %1…").arg(root.info.name));
    const auto report = m_indexer->scan(root.info, ignore.value_or(Indexer::IgnoreFn()));
    if (!report) {
        emit failed(report.error());
        return;
    }
    if (!report->failures.isEmpty())
        emit warning(tr("Could not read %n note(s) in %1", nullptr, int(report->failures.size())).arg(root.info.name));
    emit rootRescanned(root.info.path);
    reportCount();
}

void IndexService::rescanAll()
{
    QList<int> ids = m_roots.keys();
    std::sort(ids.begin(), ids.end());
    for (const int id : std::as_const(ids))
        rescanRoot(id);
}

void IndexService::verify()
{
    if (!m_indexer)
        return;
    emit busy(tr("Verifying index…"));
    QStringList lines;
    bool clean = true;
    QList<int> ids = m_roots.keys();
    std::sort(ids.begin(), ids.end());
    for (const int id : std::as_const(ids)) {
        const RootState& root = m_roots.value(id);
        if (!root.present)
            continue;
        const auto ignore = ignoreFor(id);
        const auto report = m_indexer->verify(root.info, ignore.value_or(Indexer::IgnoreFn()));
        if (!report) {
            emit failed(report.error());
            return;
        }
        if (report->clean())
            continue;
        clean = false;
        auto list = [&](const QString& label, const QStringList& paths) {
            for (const QString& path : paths)
                lines.append(QStringLiteral("%1 %2/%3").arg(label, root.info.name, path));
        };
        list(tr("not indexed:"), report->notIndexed);
        list(tr("missing on disk:"), report->notOnDisk);
        list(tr("out of date:"), report->stale);
    }
    emit verified(clean ? tr("The index matches the vault.") : lines.join(QLatin1Char('\n')), clean);
}

void IndexService::rebuild()
{
    const QString vault = m_vaultPath;
    qDeleteAll(m_watchers);
    m_watchers.clear();
    m_indexer.reset();
    m_store.reset(); // closes the database before its files are removed
    for (const char* suffix : {"", "-wal", "-shm"})
        QFile::remove(indexPath(vault) + QLatin1String(suffix));
    open(vault);
}

void IndexService::reportCount()
{
    if (const auto count = m_store->noteCount())
        emit indexChanged(*count);
}

} // namespace coeus
