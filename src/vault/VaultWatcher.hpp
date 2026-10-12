#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QSocketNotifier;
class QTimer;

namespace coeus {

// Watches one root with Linux inotify, one watch per folder (D-019). Reports
// note files that were written, created, deleted or moved, by root-relative
// path, after a quiet period (SPEC.md §Constants watcher_debounce_ms).
// Editors that save by rename (Vim, Claude Code, AtomicWriter) arrive as a
// move onto the note's name, which is reported like any other change.
// Lives on the indexer thread; never on the UI thread (AV-005).
class VaultWatcher : public QObject {
    Q_OBJECT

public:
    // Returns true for root-relative paths to leave unwatched (.gitignore).
    using IgnoreFn = std::function<bool(const QString& relativePath, bool isDir)>;

    static constexpr int kDebounceMs = 200;

    VaultWatcher(int rootId, const QString& rootPath, IgnoreFn ignore, QObject* parent = nullptr);
    ~VaultWatcher() override;

    // Adds watches for every visible, non-ignored folder. False if inotify is
    // unavailable or the watch limit was hit (see watchLimitReached).
    bool start();
    void setIgnore(IgnoreFn ignore) { m_ignore = std::move(ignore); }

    int watchCount() const { return int(m_dirs.size()); }

signals:
    // Notes that may have changed: the indexer re-reads each one.
    void filesChanged(int rootId, const QStringList& relativePaths);
    // A folder appeared, vanished or moved, a .gitignore changed, or events
    // were lost: the root needs a full scan.
    void rescanNeeded(int rootId);
    void watchLimitReached(int rootId, const QString& message);

private:
    void readEvents();
    void addTree(const QString& relativeDir, bool reportNotes);
    void removeTree(const QString& relativeDir);
    void notePath(const QString& relativePath);
    void flush();

    int m_rootId;
    QString m_rootPath;
    IgnoreFn m_ignore;
    int m_fd = -1;
    QSocketNotifier* m_notifier = nullptr;
    QTimer* m_debounce = nullptr;
    QHash<int, QString> m_dirs;   // watch descriptor -> root-relative folder ("" for the root)
    QHash<QString, int> m_watches; // root-relative folder -> watch descriptor
    QSet<QString> m_pending;
    bool m_rescanPending = false;
    bool m_limitReported = false;
};

} // namespace coeus
