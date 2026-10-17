#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <expected>

namespace coeus {

// A version of a note that sync kept beside it rather than lose (D-024):
// `copyPath` is `<notePath>.conflictN`, the other machine's text.
struct SyncConflict {
    QString notePath; // absolute; may be gone if the note was deleted meanwhile
    QString copyPath; // absolute
    bool operator==(const SyncConflict&) const = default;
};

struct SyncReport {
    QStringList changed;           // vault-relative paths the pass added, changed or removed here
    QList<SyncConflict> conflicts; // every conflict copy in the vault after the pass
    QDateTime finished;
};

struct SyncError {
    enum class Kind {
        NotConfigured, // no remote in config.toml
        ToolMissing,   // rclone not found or not runnable
        NeedsResync,   // first pass with this remote, or bisync lost its state
        SafetyAbort,   // too much would change at once; only Sync Anyway forces it
        Busy,          // another sync of this vault holds the lock
        Cancelled,     // stopped, as when Coeus quits
        Failed,
    };
    Kind kind = Kind::Failed;
    QString message; // one line for the status bar
    QString detail;  // the tool's own words, for a tooltip or a dialog
};

// Sync providers (F-028, D-007, D-024) operate on the vault, root 0, only;
// linked roots have their own sync (Git, D-010). `.coeus/`, `.git/` and
// Coeus's temporary files are never synced; `.coeus-shared/` is (D-011).
// A pass is blocking work and runs on the sync worker, never the UI thread
// (AV-005); `cancel` is checked while it runs.
class SyncProvider {
public:
    enum class Mode {
        Normal,
        Resync, // the first pass with a remote: joins both sides, keeping a conflict copy wherever they differ
        Force,  // past a safety abort, at the author's word
    };

    virtual ~SyncProvider() = default;
    virtual QString name() const = 0;
    // One pass both ways: the remote's changes in, the vault's out.
    virtual std::expected<void, SyncError> sync(const QString& vaultPath, Mode mode, const std::atomic_bool& cancel) = 0;
};

} // namespace coeus
