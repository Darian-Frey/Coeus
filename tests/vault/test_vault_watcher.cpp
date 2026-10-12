#include "vault/AtomicWriter.hpp"
#include "vault/VaultWatcher.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using coeus::VaultWatcher;

namespace {

constexpr int kWaitMs = 3000;

void write(const QString& path, const QByteArray& content, QIODevice::OpenMode mode = QIODevice::WriteOnly)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(mode))
        file.write(content);
}

// Every path reported by filesChanged so far, flattened.
QStringList reported(const QSignalSpy& spy)
{
    QStringList paths;
    for (const QList<QVariant>& args : spy)
        paths += args.at(1).toStringList();
    return paths;
}

} // namespace

class TestVaultWatcher : public QObject {
    Q_OBJECT

private slots:
    void reportsInPlaceWritesCreatesAndDeletes()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("a.md")), "one");
        write(root.filePath(QStringLiteral("sub/b.md")), "one");
        VaultWatcher watcher(3, root.path(), {});
        QSignalSpy changed(&watcher, &VaultWatcher::filesChanged);
        QVERIFY(watcher.start());
        QCOMPARE(watcher.watchCount(), 2);

        // In-place append: the case QFileSystemWatcher directory watches miss (D-019).
        write(root.filePath(QStringLiteral("a.md")), " two", QIODevice::Append);
        QVERIFY(changed.wait(kWaitMs));
        QCOMPARE(changed.front().at(0).toInt(), 3);
        QCOMPARE(reported(changed), QStringList{QStringLiteral("a.md")});

        changed.clear();
        write(root.filePath(QStringLiteral("sub/new.md")), "x");
        QFile::remove(root.filePath(QStringLiteral("sub/b.md")));
        QVERIFY(changed.wait(kWaitMs));
        QTRY_COMPARE(reported(changed), (QStringList{QStringLiteral("sub/b.md"), QStringLiteral("sub/new.md")}));
    }

    void renameSavesAreReportedOnce()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("note.md")), "old");
        VaultWatcher watcher(0, root.path(), {});
        QSignalSpy changed(&watcher, &VaultWatcher::filesChanged);
        QVERIFY(watcher.start());

        // Vim, Claude Code and AtomicWriter: temporary file renamed over the note.
        QVERIFY(coeus::AtomicWriter::write(root.filePath(QStringLiteral("note.md")), "new").has_value());
        QVERIFY(changed.wait(kWaitMs));
        QTest::qWait(VaultWatcher::kDebounceMs * 2);
        QCOMPARE(changed.size(), 1); // debounced into one batch
        QCOMPARE(reported(changed), QStringList{QStringLiteral("note.md")}); // the temp file never appears
    }

    void ignoresHiddenNonNotesAndIgnoredFolders()
    {
        QTemporaryDir root;
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("build")));
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral(".git")));
        VaultWatcher watcher(0, root.path(), [](const QString& path, bool isDir) {
            return isDir && path == QLatin1String("build");
        });
        QSignalSpy changed(&watcher, &VaultWatcher::filesChanged);
        QVERIFY(watcher.start());
        QCOMPARE(watcher.watchCount(), 1); // only the root

        write(root.filePath(QStringLiteral("build/out.md")), "x");
        write(root.filePath(QStringLiteral(".git/x.md")), "x");
        write(root.filePath(QStringLiteral("image.png")), "x");
        write(root.filePath(QStringLiteral(".hidden.md")), "x");
        QVERIFY(!changed.wait(VaultWatcher::kDebounceMs * 3));
    }

    void newFolderIsWatchedAndItsNotesReported()
    {
        QTemporaryDir root;
        VaultWatcher watcher(0, root.path(), {});
        QSignalSpy changed(&watcher, &VaultWatcher::filesChanged);
        QVERIFY(watcher.start());

        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("projects/new")));
        write(root.filePath(QStringLiteral("projects/new/README.md")), "x");
        QVERIFY(changed.wait(kWaitMs));
        QTRY_VERIFY(reported(changed).contains(QStringLiteral("projects/new/README.md")));
        QCOMPARE(watcher.watchCount(), 3);

        changed.clear();
        write(root.filePath(QStringLiteral("projects/new/later.md")), "x");
        QVERIFY(changed.wait(kWaitMs));
        QCOMPARE(reported(changed), QStringList{QStringLiteral("projects/new/later.md")});
    }

    void folderRemovalAndGitignoreEditsRequestARescan()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("old/a.md")), "x");
        VaultWatcher watcher(5, root.path(), {});
        QSignalSpy rescan(&watcher, &VaultWatcher::rescanNeeded);
        QVERIFY(watcher.start());

        QVERIFY(QDir(root.filePath(QStringLiteral("old"))).removeRecursively());
        QVERIFY(rescan.wait(kWaitMs));
        QCOMPARE(rescan.front().at(0).toInt(), 5);
        QTRY_COMPARE(watcher.watchCount(), 1);

        rescan.clear();
        write(root.filePath(QStringLiteral(".gitignore")), "drafts/\n");
        QVERIFY(rescan.wait(kWaitMs));
    }
};

QTEST_GUILESS_MAIN(TestVaultWatcher)
#include "test_vault_watcher.moc"
