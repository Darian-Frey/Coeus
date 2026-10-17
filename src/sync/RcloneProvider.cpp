#include "sync/RcloneProvider.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

namespace coeus {

namespace {

constexpr int kPollMs = 200;      // how often a running pass looks at `cancel`
constexpr int kStartMs = 10'000;

// The last line of rclone's output that says what went wrong, without its timestamp.
QString tellingLine(const QString& output)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (it->contains(QLatin1String("ERROR")) || it->contains(QLatin1String("CRITICAL")) || it->contains(QLatin1String("Fatal"))) {
            QString line = *it;
            if (const qsizetype colon = line.indexOf(QLatin1String(" : ")); colon >= 0)
                line = line.mid(colon + 3);
            else if (const qsizetype notice = line.indexOf(QLatin1String(": ")); notice >= 0 && notice < 40)
                line = line.mid(notice + 2);
            return line.trimmed();
        }
    }
    return lines.isEmpty() ? QString() : lines.back().trimmed();
}

} // namespace

RcloneProvider::RcloneProvider(QString remote, QString program)
    : m_remote(std::move(remote))
    , m_program(std::move(program))
{
}

QString RcloneProvider::workdir(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus/sync");
}

QByteArray RcloneProvider::filters()
{
    // Machine-local state, Git's own, and Coeus's and rclone's half-written files (D-011, D-024).
    return "# Written by Coeus (D-024); changing it makes bisync ask for a resync.\n"
           "- /.coeus/**\n"
           "- /.git/**\n"
           "- *.coeus-tmp-*\n"
           "- *.partial\n";
}

QString RcloneProvider::remotePath(const QString& remote, const QString& path)
{
    if (remote.endsWith(QLatin1Char(':')) || remote.endsWith(QLatin1Char('/')))
        return remote + path;
    return remote + QLatin1Char('/') + path;
}

QStringList RcloneProvider::bisyncArguments(const QString& vaultPath, const QString& remote, Mode mode)
{
    QStringList arguments{QStringLiteral("bisync"),
                          vaultPath,
                          remote,
                          QStringLiteral("--workdir"),
                          workdir(vaultPath),
                          QStringLiteral("--filters-file"),
                          workdir(vaultPath) + QStringLiteral("/filters.txt"),
                          // Where both changed: this machine's copy stays, the other is kept beside it.
                          QStringLiteral("--conflict-resolve"),
                          QStringLiteral("path1"),
                          QStringLiteral("--conflict-loser"),
                          QStringLiteral("num"),
                          QStringLiteral("--conflict-suffix"),
                          QStringLiteral("conflict"),
                          // An interrupted pass (Coeus quitting mid-sync) recovers without a resync.
                          QStringLiteral("--resilient"),
                          QStringLiteral("--recover"),
                          QStringLiteral("--color"),
                          QStringLiteral("NEVER")};
    if (mode == Mode::Resync)
        arguments << QStringLiteral("--resync");
    else if (mode == Mode::Force)
        arguments << QStringLiteral("--force");
    return arguments;
}

QString RcloneProvider::freeConflictName(const QString& absolutePath)
{
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.conflict%2").arg(absolutePath).arg(n);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

SyncError RcloneProvider::classify(int exitCode, const QString& output)
{
    const QString line = tellingLine(output);
    const auto has = [&output](const char* words) { return output.contains(QLatin1String(words), Qt::CaseInsensitive); };
    if (has("didn't find section in config file"))
        return {SyncError::Kind::Failed, QObject::tr("rclone has no such remote; set it up with rclone config"), line};
    if (has("prior lock file found"))
        return {SyncError::Kind::Busy, QObject::tr("another sync of this vault is running"), line};
    if (has("safety abort") || has("too many deletes") || has("all files were changed"))
        return {SyncError::Kind::SafetyAbort, QObject::tr("stopped: too much would change at once"), line};
    if (has("must run --resync"))
        return {SyncError::Kind::NeedsResync, QObject::tr("the first sync with this remote needs your go-ahead"), line};
    if (has("unknown flag"))
        return {SyncError::Kind::ToolMissing, QObject::tr("rclone is too old: 1.%1 or newer is needed").arg(kMinimumMinor), line};
    return {SyncError::Kind::Failed, line.isEmpty() ? QObject::tr("rclone stopped with status %1").arg(exitCode) : line, line};
}

std::expected<RcloneProvider::Run, SyncError> RcloneProvider::run(const QStringList& arguments, const std::atomic_bool& cancel) const
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProgram(m_program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(kStartMs))
        return std::unexpected(SyncError{SyncError::Kind::ToolMissing,
                                         QObject::tr("rclone was not found; install it from rclone.org"), process.errorString()});
    while (!process.waitForFinished(kPollMs)) {
        if (process.state() == QProcess::NotRunning)
            break;
        if (cancel) {
            process.terminate(); // bisync's --recover picks up after an interrupted pass
            if (!process.waitForFinished(5000))
                process.kill();
            process.waitForFinished(1000);
            return std::unexpected(SyncError{SyncError::Kind::Cancelled, QObject::tr("sync stopped"), {}});
        }
    }
    const QString output = QString::fromUtf8(process.readAll());
    if (process.exitStatus() != QProcess::NormalExit)
        return std::unexpected(SyncError{SyncError::Kind::Failed, QObject::tr("rclone stopped unexpectedly"), output.right(2000)});
    return Run{process.exitCode(), output};
}

std::expected<void, SyncError> RcloneProvider::keepRemoteVersions(const QString& vaultPath, const std::atomic_bool& cancel) const
{
    // Before a resync, which overwrites wherever both sides differ: the remote's
    // version of each such file is saved here first, as a conflict copy (D-024).
    QTemporaryDir scratch;
    const QString differ = scratch.filePath(QStringLiteral("differ.txt"));
    const auto checked = run({QStringLiteral("check"), vaultPath, m_remote, QStringLiteral("--filter-from"),
                              workdir(vaultPath) + QStringLiteral("/filters.txt"), QStringLiteral("--differ"), differ,
                              QStringLiteral("--color"), QStringLiteral("NEVER")},
                             cancel);
    if (!checked)
        return std::unexpected(checked.error());
    if (checked->exitCode != 0 && !checked->output.contains(QLatin1String("differences found")))
        return std::unexpected(classify(checked->exitCode, checked->output));
    QFile list(differ);
    if (!list.open(QIODevice::ReadOnly))
        return {}; // nothing differs
    for (const QByteArray& raw : list.readAll().split('\n')) {
        const QString path = QString::fromUtf8(raw).trimmed();
        if (path.isEmpty())
            continue;
        const QString target = freeConflictName(vaultPath + QLatin1Char('/') + path);
        const auto copied = run({QStringLiteral("copyto"), remotePath(m_remote, path), target, QStringLiteral("--color"), QStringLiteral("NEVER")},
                                cancel);
        if (!copied)
            return std::unexpected(copied.error());
        if (copied->exitCode != 0)
            return std::unexpected(classify(copied->exitCode, copied->output));
    }
    return {};
}

std::expected<void, SyncError> RcloneProvider::sync(const QString& vaultPath, Mode mode, const std::atomic_bool& cancel)
{
    if (m_remote.trimmed().isEmpty())
        return std::unexpected(SyncError{SyncError::Kind::NotConfigured, QObject::tr("no sync remote is set"), {}});
    // bisync's own files: machine-local, never synced (D-011).
    if (!QDir().mkpath(workdir(vaultPath)))
        return std::unexpected(SyncError{SyncError::Kind::Failed, QObject::tr("cannot create %1").arg(workdir(vaultPath)), {}});
    const QString filtersPath = workdir(vaultPath) + QStringLiteral("/filters.txt");
    QFile current(filtersPath);
    if (!current.open(QIODevice::ReadOnly) || current.readAll() != filters()) {
        current.close();
        QSaveFile file(filtersPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(filters()) < 0 || !file.commit())
            return std::unexpected(SyncError{SyncError::Kind::Failed, QObject::tr("cannot write %1").arg(filtersPath), {}});
    }

    if (mode == Mode::Resync) {
        // bisync cannot start against a folder that is not there yet.
        const auto made = run({QStringLiteral("mkdir"), m_remote, QStringLiteral("--color"), QStringLiteral("NEVER")}, cancel);
        if (!made)
            return std::unexpected(made.error());
        if (made->exitCode != 0)
            return std::unexpected(classify(made->exitCode, made->output));
        if (auto kept = keepRemoteVersions(vaultPath, cancel); !kept)
            return kept;
    }
    const auto ran = run(bisyncArguments(vaultPath, m_remote, mode), cancel);
    if (!ran)
        return std::unexpected(ran.error());
    if (ran->exitCode != 0)
        return std::unexpected(classify(ran->exitCode, ran->output));
    return {};
}

} // namespace coeus
