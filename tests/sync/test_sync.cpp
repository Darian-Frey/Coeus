// F-028 (D-024, AV-004): the rclone provider between two vault folders and
// a third standing in for Google Drive, conflict copies, and the sync
// service's passes. Tests needing rclone use $COEUS_RCLONE or the rclone on
// PATH, and are skipped without one; rclone's config is a temporary file,
// never the author's.

#include "sync/RcloneProvider.hpp"
#include "sync/SyncConflicts.hpp"
#include "sync/SyncService.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace coeus;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

QString rclone()
{
    const QString chosen = qEnvironmentVariable("COEUS_RCLONE");
    return chosen.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("rclone")) : chosen;
}

// A provider that does what it is told, for the service's tests.
class FakeProvider : public SyncProvider {
public:
    std::function<std::expected<void, SyncError>(const QString&, Mode)> pass;
    QString name() const override { return QStringLiteral("fake"); }
    std::expected<void, SyncError> sync(const QString& vaultPath, Mode mode, const std::atomic_bool&) override
    {
        return pass(vaultPath, mode);
    }
};

} // namespace

class TestSync : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_config;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<coeus::SyncReport>();
        qRegisterMetaType<coeus::SyncError>();
        qRegisterMetaType<QList<coeus::SyncConflict>>();
        write(m_config.filePath(QStringLiteral("rclone.conf")), "");
        qputenv("RCLONE_CONFIG", m_config.filePath(QStringLiteral("rclone.conf")).toUtf8());
    }

    void argumentsAndNames()
    {
        const QStringList args = RcloneProvider::bisyncArguments(QStringLiteral("/v"), QStringLiteral("gdrive:Coeus"), SyncProvider::Mode::Normal);
        QCOMPARE(args.mid(0, 3), (QStringList{QStringLiteral("bisync"), QStringLiteral("/v"), QStringLiteral("gdrive:Coeus")}));
        QCOMPARE(args[args.indexOf(QStringLiteral("--conflict-resolve")) + 1], QStringLiteral("path1"));
        QCOMPARE(args[args.indexOf(QStringLiteral("--workdir")) + 1], QStringLiteral("/v/.coeus/sync"));
        QVERIFY(args.contains(QStringLiteral("--recover")));
        QVERIFY(!args.contains(QStringLiteral("--resync")) && !args.contains(QStringLiteral("--force")));
        QVERIFY(RcloneProvider::bisyncArguments(QStringLiteral("/v"), QStringLiteral("r:"), SyncProvider::Mode::Resync).contains(QStringLiteral("--resync")));
        QVERIFY(RcloneProvider::bisyncArguments(QStringLiteral("/v"), QStringLiteral("r:"), SyncProvider::Mode::Force).contains(QStringLiteral("--force")));
        QVERIFY(RcloneProvider::filters().contains("- /.coeus/**"));
        QVERIFY(!RcloneProvider::filters().contains(".coeus-shared")); // the sidecar travels (D-011)

        QCOMPARE(RcloneProvider::remotePath(QStringLiteral("gdrive:Coeus"), QStringLiteral("a.md")), QStringLiteral("gdrive:Coeus/a.md"));
        QCOMPARE(RcloneProvider::remotePath(QStringLiteral("gdrive:"), QStringLiteral("a.md")), QStringLiteral("gdrive:a.md"));
        QCOMPARE(SyncConflicts::originalOf(QStringLiteral("note.md.conflict12")), std::optional<QString>(QStringLiteral("note.md")));
        QVERIFY(!SyncConflicts::originalOf(QStringLiteral("note.md")));
        QVERIFY(!SyncConflicts::originalOf(QStringLiteral("conflict1.md")));
    }

    void classifiesRcloneFailures()
    {
        using Kind = SyncError::Kind;
        QCOMPARE(RcloneProvider::classify(7, QStringLiteral("2026/10/17 20:52:41 ERROR : Bisync aborted. Must run --resync to recover.")).kind,
                 Kind::NeedsResync);
        QCOMPARE(RcloneProvider::classify(1, QStringLiteral("ERROR : Safety abort: all files were changed on Path1")).kind, Kind::SafetyAbort);
        QCOMPARE(RcloneProvider::classify(1, QStringLiteral("ERROR : too many deletes")).kind, Kind::SafetyAbort);
        QCOMPARE(RcloneProvider::classify(7, QStringLiteral("NOTICE: prior lock file found: /v/.coeus/sync/x.lck")).kind, Kind::Busy);
        const SyncError noRemote = RcloneProvider::classify(
            1, QStringLiteral("CRITICAL: Failed to create file system for \"gdrive:x\": didn't find section in config file (\"gdrive\")"));
        QCOMPARE(noRemote.kind, Kind::Failed);
        QVERIFY(noRemote.message.contains(QStringLiteral("no such remote")));
        const SyncError other = RcloneProvider::classify(3, QStringLiteral("2026/10/17 ERROR : note.md: failed to open: permission denied\n"));
        QCOMPARE(other.message, QStringLiteral("note.md: failed to open: permission denied"));
    }

    void conflictCopiesAreFoundOutsideCoeusOwnFolders()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "A");
        write(vault.filePath(QStringLiteral("a.md.conflict1")), "A elsewhere");
        write(vault.filePath(QStringLiteral("notes/b.md.conflict2")), "B elsewhere, b itself deleted");
        write(vault.filePath(QStringLiteral(".coeus/sync/x.conflict1")), "bisync's own");
        write(vault.filePath(QStringLiteral(".coeus-shared/embeddings.bin.conflict1")), "the sidecar's own");
        write(vault.filePath(QStringLiteral(".git/objects/y.conflict1")), "git's");
        const QList<SyncConflict> found = SyncConflicts::find(vault.path());
        QCOMPARE(found.size(), 2);
        QCOMPARE(found[0].notePath, vault.filePath(QStringLiteral("a.md")));
        QCOMPARE(found[0].copyPath, vault.filePath(QStringLiteral("a.md.conflict1")));
        QCOMPARE(found[1].notePath, vault.filePath(QStringLiteral("notes/b.md")));
        QCOMPARE(RcloneProvider::freeConflictName(vault.filePath(QStringLiteral("a.md"))), vault.filePath(QStringLiteral("a.md.conflict2")));
        // The synced files: the sidecar if asked, never .coeus/, .git/ or temporary files.
        write(vault.filePath(QStringLiteral(".a.md.coeus-tmp-123")), "half");
        QStringList names;
        for (const QFileInfo& file : SyncConflicts::syncedFiles(vault.path(), true))
            names.append(QDir(vault.path()).relativeFilePath(file.absoluteFilePath()));
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral(".coeus-shared/embeddings.bin.conflict1"), QStringLiteral("a.md"),
                                     QStringLiteral("a.md.conflict1"), QStringLiteral("notes/b.md.conflict2")}));
    }

    void twoMachinesThroughOneRemote()
    {
        const QString program = rclone();
        if (program.isEmpty())
            QSKIP("rclone is not installed (or set COEUS_RCLONE)");
        QTemporaryDir desktop, laptop, drive;
        const QString remote = drive.filePath(QStringLiteral("Coeus")); // a folder not there yet
        RcloneProvider provider(remote, program);
        const std::atomic_bool keepGoing = false;
        const auto at = [](const QTemporaryDir& dir, const char* path) { return dir.filePath(QLatin1String(path)); };

        write(at(desktop, "note.md"), "# Note\n\nfirst\n");
        write(at(desktop, "other.md"), "# Other\n");
        write(at(desktop, "third.md"), "# Third\n");
        write(at(desktop, ".coeus/config.toml"), "[sync]\n");
        write(at(desktop, ".coeus-shared/embeddings.bin"), "vectors");
        write(at(desktop, ".note.md.coeus-tmp-1"), "half-saved");

        // The first pass needs the author's word.
        const auto first = provider.sync(desktop.path(), SyncProvider::Mode::Normal, keepGoing);
        QVERIFY(!first);
        QCOMPARE(first.error().kind, SyncError::Kind::NeedsResync);
        auto joined = provider.sync(desktop.path(), SyncProvider::Mode::Resync, keepGoing);
        QVERIFY2(joined, qPrintable(joined ? QString() : joined.error().message + QLatin1Char(' ') + joined.error().detail));
        QCOMPARE(read(remote + QStringLiteral("/note.md")), QByteArray("# Note\n\nfirst\n"));
        QCOMPARE(read(remote + QStringLiteral("/.coeus-shared/embeddings.bin")), QByteArray("vectors")); // D-011
        QVERIFY(!QFileInfo::exists(remote + QStringLiteral("/.coeus")));                                   // never synced
        QVERIFY(!QFileInfo::exists(remote + QStringLiteral("/.note.md.coeus-tmp-1")));

        // The laptop joins holding an older, different note.md and a note of its own:
        // its own stays, the remote's is kept beside it, nothing is lost (AV-004).
        write(at(laptop, "note.md"), "# Note\n\nold copy\n");
        write(at(laptop, "laptop.md"), "# Laptop\n");
        joined = provider.sync(laptop.path(), SyncProvider::Mode::Resync, keepGoing);
        QVERIFY2(joined, qPrintable(joined ? QString() : joined.error().message + QLatin1Char(' ') + joined.error().detail));
        QCOMPARE(read(at(laptop, "note.md")), QByteArray("# Note\n\nold copy\n"));
        QCOMPARE(read(at(laptop, "note.md.conflict1")), QByteArray("# Note\n\nfirst\n"));
        QCOMPARE(read(at(laptop, "other.md")), QByteArray("# Other\n"));
        QCOMPARE(read(at(laptop, ".coeus-shared/embeddings.bin")), QByteArray("vectors"));
        QCOMPARE(read(remote + QStringLiteral("/laptop.md")), QByteArray("# Laptop\n"));
        QCOMPARE(SyncConflicts::find(laptop.path()).size(), 1);

        // Ordinary passes: an edit and a deletion travel.
        QVERIFY(provider.sync(desktop.path(), SyncProvider::Mode::Normal, keepGoing));
        QCOMPARE(read(at(desktop, "laptop.md")), QByteArray("# Laptop\n"));
        QCOMPARE(read(at(desktop, "note.md.conflict1")), QByteArray("# Note\n\nfirst\n"));
        write(at(desktop, "other.md"), "# Other\n\nedited on the desktop\n");
        QFile::remove(at(desktop, "third.md"));
        QVERIFY(provider.sync(desktop.path(), SyncProvider::Mode::Normal, keepGoing));
        QVERIFY(provider.sync(laptop.path(), SyncProvider::Mode::Normal, keepGoing));
        QCOMPARE(read(at(laptop, "other.md")), QByteArray("# Other\n\nedited on the desktop\n"));
        QVERIFY(!QFileInfo::exists(at(laptop, "third.md")));

        // Both edit the same note: the machine that meets it keeps its own as the
        // note, the other's arrives beside it, everywhere (D-024).
        write(at(desktop, "laptop.md"), "# Laptop\n\nfrom the desktop\n");
        QThread::msleep(1100); // a later modification time, as on two real machines
        write(at(laptop, "laptop.md"), "# Laptop\n\nfrom the laptop\n");
        QVERIFY(provider.sync(desktop.path(), SyncProvider::Mode::Normal, keepGoing));
        QVERIFY(provider.sync(laptop.path(), SyncProvider::Mode::Normal, keepGoing));
        QVERIFY(provider.sync(desktop.path(), SyncProvider::Mode::Normal, keepGoing));
        for (const QTemporaryDir* machine : {&desktop, &laptop}) {
            QCOMPARE(read(machine->filePath(QStringLiteral("laptop.md"))), QByteArray("# Laptop\n\nfrom the laptop\n"));
            QCOMPARE(read(machine->filePath(QStringLiteral("laptop.md.conflict1"))), QByteArray("# Laptop\n\nfrom the desktop\n"));
        }
        const QList<SyncConflict> conflicts = SyncConflicts::find(desktop.path());
        QCOMPARE(conflicts.size(), 2); // note.md's from joining, laptop.md's now
        QCOMPARE(conflicts[0].notePath, at(desktop, "laptop.md"));

        // A remote rclone has never heard of.
        const auto unknown = RcloneProvider(QStringLiteral("nosuchremote:Coeus"), program).sync(desktop.path(), SyncProvider::Mode::Resync, keepGoing);
        QVERIFY(!unknown);
        QVERIFY(unknown.error().message.contains(QStringLiteral("no such remote")));
        // And rclone missing altogether.
        const auto missing = RcloneProvider(remote, QStringLiteral("/nonexistent/rclone")).sync(desktop.path(), SyncProvider::Mode::Normal, keepGoing);
        QCOMPARE(missing.error().kind, SyncError::Kind::ToolMissing);
    }

    void serviceReportsPassesAndWaitsForTheAuthor()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "A");
        int passes = 0;
        bool needsResync = true;
        SyncService service(vault.path(), [&](const QString&) {
            auto fake = std::make_unique<FakeProvider>();
            fake->pass = [&](const QString& path, SyncProvider::Mode mode) -> std::expected<void, SyncError> {
                ++passes;
                if (needsResync && mode != SyncProvider::Mode::Resync)
                    return std::unexpected(SyncError{SyncError::Kind::NeedsResync, QStringLiteral("first"), {}});
                needsResync = false;
                write(path + QStringLiteral("/b.md"), "B, from the remote");
                write(path + QStringLiteral("/a.md.conflict1"), "A elsewhere");
                return {};
            };
            return fake;
        });
        QVERIFY(service.isConfigured());
        QSignalSpy failed(&service, &SyncService::failed);
        QSignalSpy finished(&service, &SyncService::finished);
        QSignalSpy conflicts(&service, &SyncService::conflictsChanged);
        // The first pass comes by itself, and needs the author's word.
        QVERIFY(failed.wait(SyncService::kFirstPassDelayMs + 5000));
        QCOMPARE(failed.front().at(0).value<SyncError>().kind, SyncError::Kind::NeedsResync);
        QCOMPARE(service.lastError()->kind, SyncError::Kind::NeedsResync);
        QTest::qWait(300);
        QCOMPARE(passes, 1); // no retrying on a timer meanwhile

        QVERIFY(service.syncNow(SyncProvider::Mode::Resync));
        QVERIFY(service.isRunning());
        QVERIFY(!service.syncNow()); // one at a time
        QVERIFY(finished.wait(5000));
        const SyncReport report = finished.front().at(0).value<SyncReport>();
        QCOMPARE(report.changed, (QStringList{QStringLiteral("a.md.conflict1"), QStringLiteral("b.md")}));
        QCOMPARE(report.conflicts.size(), 1);
        QCOMPARE(service.conflicts().size(), 1);
        QVERIFY(!conflicts.isEmpty());
        QVERIFY(!service.lastError());

        // Resolved elsewhere: a fresh look finds none.
        QFile::remove(vault.filePath(QStringLiteral("a.md.conflict1")));
        conflicts.clear();
        service.refreshConflicts();
        QVERIFY(conflicts.wait(5000));
        QVERIFY(service.conflicts().isEmpty());
    }

    void notConfiguredDoesNothing()
    {
        QTemporaryDir vault;
        SyncService service(vault.path()); // config.toml has no [sync] remote
        QVERIFY(!service.isConfigured());
        QVERIFY(!service.syncNow());
        QSignalSpy started(&service, &SyncService::started);
        QTest::qWait(200);
        QVERIFY(started.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "test_sync.moc"
