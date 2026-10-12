#include "IndexDump.hpp"

#include "index/Database.hpp"
#include "index/IndexService.hpp"
#include "vault/Roots.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <git2.h>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

constexpr int kWaitMs = 5000;

bool initRepository(const QString& path)
{
    git_libgit2_init();
    git_repository* repo = nullptr;
    const int rc = git_repository_init(&repo, QFile::encodeName(path).constData(), 0);
    git_repository_free(repo);
    git_libgit2_shutdown();
    return rc == 0;
}

void writeConfig(const QString& vault, const QByteArray& toml)
{
    writeFile(Roots::configPath(vault), toml);
}

// Paths ("root:path") of every indexed note, read through a second connection.
QStringList notes(const QString& vault)
{
    auto db = Database::open(IndexService::indexPath(vault), Database::Mode::ReadOnly);
    if (!db)
        return {db.error()};
    auto q = db->prepare("SELECT r.name || ':' || n.path FROM notes n JOIN roots r ON r.id = n.root_id ORDER BY 1");
    QStringList rows;
    while (q->step().value_or(false))
        rows.append(q->text(0));
    return rows;
}

} // namespace

class TestIndexService : public QObject {
    Q_OBJECT

private slots:
    void opensVaultWithLinkedRootsAndGitignore()
    {
        QTemporaryDir vault, repo;
        QVERIFY(initRepository(repo.path()));
        writeFile(vault.filePath(QStringLiteral("home.md")), "[[README]]\n");
        writeFile(repo.filePath(QStringLiteral("README.md")), "# Repo\n");
        writeFile(repo.filePath(QStringLiteral(".gitignore")), "build/\n");
        writeFile(repo.filePath(QStringLiteral("build/_deps/lib/README.md")), "third party\n");
        writeConfig(vault.path(), QStringLiteral("[[roots]]\nname = \"repo\"\npath = \"%1\"\n"
                                                 "[[roots]]\nname = \"elsewhere\"\npath = \"/nonexistent/coeus-test\"\n")
                                      .arg(repo.path())
                                      .toUtf8());

        IndexService service;
        QSignalSpy opened(&service, &IndexService::opened);
        service.open(vault.path());
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.front().at(0).toInt(), 2);
        const QStringList problems = opened.front().at(1).toStringList();
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.front().contains(QStringLiteral("elsewhere")));
        QCOMPARE(notes(vault.path()), (QStringList{QStringLiteral("repo:README.md"), QStringLiteral("vault:home.md")}));
    }

    void watcherKeepsIndexCurrent()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("a.md")), "x");
        IndexService service;
        QSignalSpy changed(&service, &IndexService::indexChanged);
        service.open(vault.path());

        writeFile(vault.filePath(QStringLiteral("b.md")), "x");
        QTRY_VERIFY_WITH_TIMEOUT(notes(vault.path()).contains(QStringLiteral("vault:b.md")), kWaitMs);
        QVERIFY(!changed.isEmpty());

        QFile::remove(vault.filePath(QStringLiteral("a.md")));
        QTRY_COMPARE_WITH_TIMEOUT(notes(vault.path()), QStringList{QStringLiteral("vault:b.md")}, kWaitMs);
    }

    void gitignoreEditTriggersRescan()
    {
        QTemporaryDir vault;
        QVERIFY(initRepository(vault.path()));
        writeFile(vault.filePath(QStringLiteral(".gitignore")), "drafts/\n");
        writeFile(vault.filePath(QStringLiteral("drafts/d.md")), "x");
        writeFile(vault.filePath(QStringLiteral("n.md")), "x");
        IndexService service;
        service.open(vault.path());
        QCOMPARE(notes(vault.path()), QStringList{QStringLiteral("vault:n.md")});

        writeFile(vault.filePath(QStringLiteral(".gitignore")), "\n");
        QTRY_COMPARE_WITH_TIMEOUT(notes(vault.path()),
                                  (QStringList{QStringLiteral("vault:drafts/d.md"), QStringLiteral("vault:n.md")}), kWaitMs);
    }

    void missingRootKeepsItsNotes()
    {
        // AV-010: a root absent on this machine is never scanned, so never emptied.
        QTemporaryDir vault, repo;
        writeFile(repo.filePath(QStringLiteral("kept.md")), "x");
        writeConfig(vault.path(), QStringLiteral("[[roots]]\nname = \"repo\"\npath = \"%1\"\n").arg(repo.path()).toUtf8());
        {
            IndexService service;
            service.open(vault.path());
        }
        QVERIFY(notes(vault.path()).contains(QStringLiteral("repo:kept.md")));

        writeConfig(vault.path(), "[[roots]]\nname = \"repo\"\npath = \"/nonexistent/coeus-laptop-path\"\n");
        {
            IndexService service;
            service.open(vault.path());
        }
        QVERIFY(notes(vault.path()).contains(QStringLiteral("repo:kept.md")));
        auto db = Database::open(IndexService::indexPath(vault.path()), Database::Mode::ReadOnly);
        auto q = db->prepare("SELECT present FROM roots WHERE name = 'repo'");
        QVERIFY(q->step().value());
        QCOMPARE(q->int64(0), std::int64_t(0));

        // Removing the root from config drops its notes.
        writeConfig(vault.path(), "\n");
        {
            IndexService service;
            service.open(vault.path());
        }
        QVERIFY(!notes(vault.path()).contains(QStringLiteral("repo:kept.md")));
    }

    void verifyAndRebuild()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("a.md")), "[[b]]\n");
        writeFile(vault.filePath(QStringLiteral("b.md")), "x");
        IndexService service;
        QSignalSpy verified(&service, &IndexService::verified);
        QSignalSpy opened(&service, &IndexService::opened);
        service.open(vault.path());

        service.verify();
        QCOMPARE(verified.size(), 1);
        QVERIFY(verified.front().at(1).toBool());

        const QStringList before = notes(vault.path());
        service.rebuild();
        QCOMPARE(opened.size(), 2);
        QCOMPARE(notes(vault.path()), before);
    }
};

QTEST_GUILESS_MAIN(TestIndexService)
#include "test_index_service.moc"
