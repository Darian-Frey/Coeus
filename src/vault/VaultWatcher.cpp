#include "vault/VaultWatcher.hpp"

#include "vault/Vault.hpp"
#include "vault/VaultLogging.hpp"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <climits>
#include <cstring>

#include <sys/inotify.h>
#include <unistd.h>

namespace coeus {

namespace {

constexpr uint32_t kDirMask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB
                              | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW | IN_EXCL_UNLINK;

QString join(const QString& dir, const QString& name)
{
    return dir.isEmpty() ? name : dir + QLatin1Char('/') + name;
}

bool isHidden(const QString& name)
{
    return name.startsWith(QLatin1Char('.'));
}

} // namespace

VaultWatcher::VaultWatcher(int rootId, const QString& rootPath, IgnoreFn ignore, QObject* parent)
    : QObject(parent)
    , m_rootId(rootId)
    , m_rootPath(rootPath)
    , m_ignore(std::move(ignore))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &VaultWatcher::flush);
}

VaultWatcher::~VaultWatcher()
{
    if (m_fd >= 0)
        ::close(m_fd); // removes every watch
}

bool VaultWatcher::start()
{
    m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
        emit watchLimitReached(m_rootId, QStringLiteral("inotify unavailable: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &VaultWatcher::readEvents);
    addTree(QString(), false);
    return !m_limitReported;
}

void VaultWatcher::addTree(const QString& relativeDir, bool reportNotes)
{
    if (m_watches.contains(relativeDir))
        return;
    const QString absolute = relativeDir.isEmpty() ? m_rootPath : m_rootPath + QLatin1Char('/') + relativeDir;
    const int wd = inotify_add_watch(m_fd, QFile::encodeName(absolute).constData(), kDirMask);
    if (wd < 0) {
        if ((errno == ENOSPC || errno == ENOMEM) && !m_limitReported) {
            m_limitReported = true;
            emit watchLimitReached(m_rootId,
                                   QStringLiteral("inotify watch limit reached after %1 folders; changes in other "
                                                  "folders will not be seen. See BUILD.md §Large vaults.")
                                       .arg(m_dirs.size()));
        }
        return;
    }
    m_dirs.insert(wd, relativeDir);
    m_watches.insert(relativeDir, wd);

    const QDir dir(absolute);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QString relative = join(relativeDir, entry.fileName());
        if (isHidden(entry.fileName()))
            continue;
        if (entry.isDir() && !entry.isSymLink()) {
            if (!m_ignore || !m_ignore(relative, true))
                addTree(relative, reportNotes);
        } else if (reportNotes && Vault::isNote(entry.fileName())) {
            // Files created before the folder's watch existed.
            notePath(relative);
        }
    }
}

void VaultWatcher::removeTree(const QString& relativeDir)
{
    const QString prefix = relativeDir + QLatin1Char('/');
    for (auto it = m_watches.begin(); it != m_watches.end();) {
        if (it.key() == relativeDir || it.key().startsWith(prefix)) {
            inotify_rm_watch(m_fd, it.value());
            m_dirs.remove(it.value());
            it = m_watches.erase(it);
        } else {
            ++it;
        }
    }
}

void VaultWatcher::readEvents()
{
    alignas(inotify_event) char buffer[64 * (sizeof(inotify_event) + NAME_MAX + 1)];
    for (;;) {
        const ssize_t length = ::read(m_fd, buffer, sizeof buffer);
        if (length <= 0)
            break; // EAGAIN: drained

        for (char* p = buffer; p < buffer + length;) {
            const auto* event = reinterpret_cast<const inotify_event*>(p);
            p += sizeof(inotify_event) + event->len;

            if (event->mask & IN_Q_OVERFLOW) {
                qCWarning(lcVault, "inotify queue overflowed; rescanning root %d", m_rootId);
                m_rescanPending = true;
                continue;
            }
            if (event->mask & IN_IGNORED) {
                const QString dir = m_dirs.take(event->wd);
                m_watches.remove(dir);
                continue;
            }
            const auto dirIt = m_dirs.constFind(event->wd);
            if (dirIt == m_dirs.constEnd())
                continue;
            const QString dir = *dirIt;

            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                if (!dir.isEmpty())
                    m_rescanPending = true; // the parent's event covers the rest
                continue;
            }
            if (event->len == 0)
                continue;

            const QString name = QFile::decodeName(event->name);
            const QString relative = join(dir, name);
            if (name == QLatin1String(".gitignore")) {
                m_rescanPending = true;
                continue;
            }
            if (isHidden(name))
                continue; // .coeus/, .git/, AtomicWriter temporary files

            if (event->mask & IN_ISDIR) {
                if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                    if (!m_ignore || !m_ignore(relative, true))
                        addTree(relative, true);
                    if (event->mask & IN_MOVED_TO)
                        m_rescanPending = true; // a moved-in folder may replace indexed notes
                } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                    removeTree(relative);
                    m_rescanPending = true;
                }
                continue;
            }
            if (Vault::isNote(name))
                notePath(relative);
        }
    }
    if (m_rescanPending || !m_pending.isEmpty())
        m_debounce->start();
}

void VaultWatcher::notePath(const QString& relativePath)
{
    if (m_ignore && m_ignore(relativePath, false))
        return;
    m_pending.insert(relativePath);
    m_debounce->start();
}

void VaultWatcher::flush()
{
    if (m_rescanPending) {
        m_rescanPending = false;
        m_pending.clear(); // the scan covers them
        emit rescanNeeded(m_rootId);
        return;
    }
    if (m_pending.isEmpty())
        return;
    QStringList paths(m_pending.begin(), m_pending.end());
    paths.sort();
    m_pending.clear();
    emit filesChanged(m_rootId, paths);
}

} // namespace coeus
