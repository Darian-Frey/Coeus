// F-022: status, committing chosen paths, and pushing with git (D-020).

#include "GitFixture.hpp"

#include "git/GitPush.hpp"
#include "git/GitRepository.hpp"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr qint64 kStart = 1'790'000'000;

QStringList brief(const QList<GitChange>& changes)
{
    QStringList out;
    for (const GitChange& change : changes)
        out.append(change.letter() + (change.staged ? QStringLiteral("+") : QString()) + QLatin1Char(' ') + change.path);
    return out;
}

} // namespace

class TestGitCommit : public QObject {
    Q_OBJECT

private slots:
    void statusListsChangesButNotCoeusFiles()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.write(QStringLiteral(".gitignore"), "build/\n");
        git.write(QStringLiteral("a.md"), "a\n");
        git.write(QStringLiteral("gone.md"), "g\n");
        git.commit(QStringLiteral("Base"), {QStringLiteral(".gitignore"), QStringLiteral("a.md"), QStringLiteral("gone.md")}, kStart);
        git.write(QStringLiteral("a.md"), "a2\n");
        git.remove(QStringLiteral("gone.md"));
        git.write(QStringLiteral("notes/new.md"), "new\n");
        git.write(QStringLiteral("staged.md"), "s\n");
        git.stage({QStringLiteral("staged.md")});
        git.write(QStringLiteral("build/out.md"), "ignored\n");
        git.write(QStringLiteral(".coeus/index.db"), "derived\n");
        git.write(QStringLiteral("notes/.new.md.coeus-tmp-1a2b"), "temp\n");

        const auto changes = GitRepository::discover(dir.path())->status();
        QVERIFY2(changes, changes ? "" : qPrintable(changes.error()));
        QCOMPARE(brief(*changes), (QStringList{QStringLiteral("M a.md"), QStringLiteral("D gone.md"),
                                               QStringLiteral("? notes/new.md"), QStringLiteral("A+ staged.md")}));
    }

    void commitsOnlyTheChosenPaths()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.setIdentity();
        git.write(QStringLiteral("a.md"), "a\n");
        git.write(QStringLiteral("b.md"), "b\n");
        git.write(QStringLiteral("old.md"), "old\n");
        const QString base = git.commit(QStringLiteral("Base"), {QStringLiteral("a.md"), QStringLiteral("b.md"), QStringLiteral("old.md")}, kStart);
        git.write(QStringLiteral("a.md"), "a2\n");
        git.write(QStringLiteral("b.md"), "b2\n");
        git.remove(QStringLiteral("old.md"));
        git.write(QStringLiteral("other.md"), "staged elsewhere\n");
        git.stage({QStringLiteral("other.md")});

        auto repo = GitRepository::discover(dir.path());
        const auto made = repo->commit({QStringLiteral("a.md"), QStringLiteral("old.md")},
                                       QStringLiteral("  Update a, drop old\r\n\r\nWhy: testing.  \n"));
        QVERIFY2(made, made ? "" : qPrintable(made.error()));
        QCOMPARE(made->message, QStringLiteral("Update a, drop old\n\nWhy: testing."));
        QCOMPARE(made->author, QStringLiteral("Grace Hopper"));
        QCOMPARE(GitFixture::resolve(dir.path(), "HEAD"), made->id);
        QCOMPARE(GitFixture::resolve(dir.path(), "HEAD~1"), base);

        QCOMPARE(repo->fileAt(QStringLiteral("HEAD"), QStringLiteral("a.md")).value(), QByteArray("a2\n"));
        QCOMPARE(repo->fileAt(QStringLiteral("HEAD"), QStringLiteral("b.md")).value(), QByteArray("b\n")); // not chosen
        QVERIFY(!repo->fileAt(QStringLiteral("HEAD"), QStringLiteral("old.md")));                          // deleted
        QVERIFY(!repo->fileAt(QStringLiteral("HEAD"), QStringLiteral("other.md")));                        // staged, not chosen
        // What was left out is as it was: b modified, other.md still staged.
        QCOMPARE(brief(*repo->status()), (QStringList{QStringLiteral("M b.md"), QStringLiteral("A+ other.md")}));
    }

    void theFirstCommitAndRefusals()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.setIdentity();
        git.write(QStringLiteral("first.md"), "1\n");
        git.write(QStringLiteral("later.md"), "2\n");
        auto repo = GitRepository::discover(dir.path());
        QVERIFY(!repo->commit({QStringLiteral("first.md")}, QStringLiteral("   \n")));      // no message
        QVERIFY(!repo->commit({}, QStringLiteral("Nothing")));                             // nothing chosen
        QVERIFY(!repo->commit({QStringLiteral(".coeus/index.db")}, QStringLiteral("No"))); // Coeus's own
        QVERIFY(!repo->commit({QStringLiteral("../outside.md")}, QStringLiteral("No")));   // outside the tree
        const auto first = repo->commit({QStringLiteral("first.md")}, QStringLiteral("Begin"));
        QVERIFY2(first, first ? "" : qPrintable(first.error()));
        QCOMPARE(repo->branch(), QStringLiteral("main"));
        QVERIFY(!repo->fileAt(QStringLiteral("HEAD"), QStringLiteral("later.md")));
        const auto again = repo->commit({QStringLiteral("first.md")}, QStringLiteral("Again"));
        QVERIFY(!again);
        QVERIFY(again.error().contains(QStringLiteral("unchanged")));
    }

    void pushesWithGit()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is not installed");
        QTemporaryDir dir;
        const QString remote = dir.filePath(QStringLiteral("remote.git"));
        QVERIFY(GitFixture::initBare(remote));
        GitFixture git(dir.filePath(QStringLiteral("work")));
        git.setIdentity();
        git.write(QStringLiteral("a.md"), "a\n");
        const QString id = git.commit(QStringLiteral("To push"), {QStringLiteral("a.md")}, kStart);

        GitPush push;
        QSignalSpy finished(&push, &GitPush::finished);
        // No remote yet: git's own reason comes back.
        QVERIFY(push.start(dir.filePath(QStringLiteral("work"))));
        QVERIFY(finished.wait(30'000));
        QCOMPARE(finished.front().at(0).toBool(), false);
        QVERIFY2(finished.front().at(1).toString().contains(QStringLiteral("No configured push destination")), qPrintable(finished.front().at(1).toString()));

        git.setUpstream(remote);
        finished.clear();
        QVERIFY(push.start(dir.filePath(QStringLiteral("work"))));
        QVERIFY(!push.start(dir.filePath(QStringLiteral("work")))); // one at a time
        QVERIFY(finished.wait(30'000));
        QVERIFY2(finished.front().at(0).toBool(), qPrintable(finished.front().at(1).toString()));
        QVERIFY(finished.front().at(1).toString().contains(QStringLiteral("main -> main")));
        QCOMPARE(GitFixture::resolve(remote, "refs/heads/main"), id);

        // Ahead/behind against the upstream, as of the last fetch (git push updated it).
        auto repo = GitRepository::discover(dir.filePath(QStringLiteral("work")));
        auto tracking = repo->tracking();
        QVERIFY(tracking);
        QCOMPARE(tracking->upstream, QStringLiteral("origin/main"));
        QCOMPARE(tracking->ahead, 0);
        QCOMPARE(tracking->behind, 0);
        git.write(QStringLiteral("a.md"), "a2\n");
        git.commit(QStringLiteral("Local only"), {QStringLiteral("a.md")}, kStart + 86400);
        tracking = repo->tracking();
        QCOMPARE(tracking->ahead, 1);
        QCOMPARE(tracking->behind, 0);
        QVERIFY(git.detach(id));
        QVERIFY(!repo->tracking()); // detached: no branch to track
    }
};

QTEST_GUILESS_MAIN(TestGitCommit)
#include "test_git_commit.moc"
