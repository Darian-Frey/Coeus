#pragma once

#include "sync/SyncProvider.hpp"

#include <QStringList>

namespace coeus {

// The first sync provider (F-028, D-024): one `rclone bisync` pass between
// the vault and an rclone remote, such as `gdrive:Coeus`. Where a file
// changed on both sides, this machine's copy stays and the other arrives
// beside it as `<name>.conflictN`. bisync keeps its state in
// `.coeus/sync/`. A resync, the first pass with a remote, first saves the
// remote's version of every file that differs here as a conflict copy, so
// nothing is overwritten that is not also kept.
class RcloneProvider : public SyncProvider {
public:
    static constexpr int kMinimumMinor = 66; // rclone 1.66 brought bisync's conflict flags

    explicit RcloneProvider(QString remote, QString program = QStringLiteral("rclone"));

    QString name() const override { return QStringLiteral("rclone"); }
    std::expected<void, SyncError> sync(const QString& vaultPath, Mode mode, const std::atomic_bool& cancel) override;

    // Pure parts, for tests.
    static QString workdir(const QString& vaultPath);
    static QByteArray filters();
    static QStringList bisyncArguments(const QString& vaultPath, const QString& remote, Mode mode);
    // `remote` + `path`, joined as rclone wants: "gdrive:Coeus/a.md", "gdrive:a.md", "/mnt/nas/a.md".
    static QString remotePath(const QString& remote, const QString& path);
    // What a failed bisync means, from its exit code and words.
    static SyncError classify(int exitCode, const QString& output);
    // The first free `<path>.conflictN` (N from 1).
    static QString freeConflictName(const QString& absolutePath);

private:
    struct Run {
        int exitCode = 0;
        QString output;
    };
    std::expected<Run, SyncError> run(const QStringList& arguments, const std::atomic_bool& cancel) const;
    std::expected<void, SyncError> keepRemoteVersions(const QString& vaultPath, const std::atomic_bool& cancel) const;

    QString m_remote;
    QString m_program;
};

} // namespace coeus
