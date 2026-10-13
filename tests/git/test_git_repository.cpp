// F-020: the libgit2 wrapper, against scratch repositories.

#include "GitFixture.hpp"

#include "git/GitRepository.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr qint64 kDay = 86400;
constexpr qint64 kStart = 1'790'000'000; // 2026-09-21

QStringList summaries(const QList<GitCommit>& history)
{
    QStringList out;
    for (const GitCommit& commit : history)
        out.append(commit.summary + QLatin1Char('@') + commit.path);
    return out;
}

} // namespace

class TestGitRepository : public QObject {
    Q_OBJECT

private slots:
    void discoversFromAnySubfolder()
    {
        QTemporaryDir dir;
        GitFixture git(dir.filePath(QStringLiteral("repo")));
        QVERIFY(git.valid());
        git.write(QStringLiteral("notes/deep/a.md"), "A\n");
        const auto repo = GitRepository::discover(dir.filePath(QStringLiteral("repo/notes/deep/a.md")));
        QVERIFY2(repo, repo ? "" : qPrintable(repo.error()));
        QCOMPARE(repo->workdir(), QFileInfo(dir.filePath(QStringLiteral("repo"))).canonicalFilePath());
        QCOMPARE(repo->relativePath(dir.filePath(QStringLiteral("repo/notes/deep/a.md"))), QStringLiteral("notes/deep/a.md"));
        QCOMPARE(repo->branch(), QStringLiteral("main")); // unborn: no commit yet
        const auto unborn = repo->fileHistory(QStringLiteral("notes/deep/a.md"));
        QVERIFY2(unborn, unborn ? "" : qPrintable(unborn.error()));
        QCOMPARE(unborn->size(), 0);
        QVERIFY(!GitRepository::discover(QDir::rootPath() + QStringLiteral("proc")));
    }

    void historyListsTheCommitsThatChangedTheFile()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.write(QStringLiteral("a.md"), "one\n");
        git.write(QStringLiteral("b.md"), "b\n");
        const QString first = git.commit(QStringLiteral("Add a and b\n\nWith a body."), {QStringLiteral("a.md"), QStringLiteral("b.md")}, kStart);
        git.write(QStringLiteral("a.md"), "two\n");
        const QString second = git.commit(QStringLiteral("Edit a"), {QStringLiteral("a.md")}, kStart + kDay);
        git.write(QStringLiteral("b.md"), "b2\n");
        git.commit(QStringLiteral("Edit b only"), {QStringLiteral("b.md")}, kStart + 2 * kDay);
        git.write(QStringLiteral("a.md"), "three, unsaved in Git\n");

        const auto repo = GitRepository::discover(dir.path());
        const auto history = repo->fileHistory(QStringLiteral("a.md"));
        QVERIFY(history);
        QCOMPARE(summaries(*history), (QStringList{QStringLiteral("Edit a@a.md"), QStringLiteral("Add a and b@a.md")}));
        const GitCommit& newest = history->front();
        QCOMPARE(newest.id, second);
        QCOMPARE(newest.shortId, second.left(7));
        QCOMPARE(newest.author, QStringLiteral("Ada Lovelace"));
        QCOMPARE(newest.time.toSecsSinceEpoch(), kStart + kDay);
        QCOMPARE(newest.time.offsetFromUtc(), 3600);
        QCOMPARE(history->back().message, QStringLiteral("Add a and b\n\nWith a body."));
        QCOMPARE(repo->fileAt(first, QStringLiteral("a.md")).value(), QByteArray("one\n"));
        QCOMPARE(repo->fileAt(second.left(7), QStringLiteral("a.md")).value(), QByteArray("two\n"));
        QVERIFY(!repo->fileAt(first, QStringLiteral("missing.md")));
        QVERIFY(!repo->fileAt(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), QStringLiteral("a.md")));
        QCOMPARE(repo->fileHistory(QStringLiteral("untracked.md"))->size(), 0);
        QCOMPARE(repo->fileHistory(QStringLiteral("a.md"), 1)->size(), 1); // limit
    }

    void renamesAreFollowed()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        const QByteArray body = "# Plan\n\nA long enough body that a rename is recognised by similarity.\nLine three.\n";
        git.write(QStringLiteral("draft.md"), body);
        git.commit(QStringLiteral("Draft"), {QStringLiteral("draft.md")}, kStart);
        git.write(QStringLiteral("draft.md"), body + "Line four.\n");
        git.commit(QStringLiteral("Extend"), {QStringLiteral("draft.md")}, kStart + kDay);
        git.remove(QStringLiteral("draft.md"));
        git.write(QStringLiteral("plans/final.md"), body + "Line four.\n");
        git.commit(QStringLiteral("Rename"), {QStringLiteral("draft.md"), QStringLiteral("plans/final.md")}, kStart + 2 * kDay);

        const auto repo = GitRepository::discover(dir.path());
        const auto history = repo->fileHistory(QStringLiteral("plans/final.md"));
        QCOMPARE(summaries(*history), (QStringList{QStringLiteral("Rename@plans/final.md"), QStringLiteral("Extend@draft.md"),
                                                   QStringLiteral("Draft@draft.md")}));
        QCOMPARE(repo->fileAt(history->back().id, history->back().path).value(), body);
    }

    void aMergeThatTookOneSideIsSkipped()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.write(QStringLiteral("a.md"), "base\n");
        const QString base = git.commit(QStringLiteral("Base"), {QStringLiteral("a.md")}, kStart);
        git.write(QStringLiteral("a.md"), "side\n");
        const QString side = git.commit(QStringLiteral("Side edits a"), {QStringLiteral("a.md")}, kStart + kDay, {base}, false);
        git.write(QStringLiteral("a.md"), "base\n");
        git.write(QStringLiteral("c.md"), "c\n");
        const QString main = git.commit(QStringLiteral("Main adds c"), {QStringLiteral("a.md"), QStringLiteral("c.md")}, kStart + 2 * kDay);
        git.write(QStringLiteral("a.md"), "side\n");
        git.commit(QStringLiteral("Merge side"), {QStringLiteral("a.md")}, kStart + 3 * kDay, {main, side});

        const auto repo = GitRepository::discover(dir.path());
        QCOMPARE(summaries(*repo->fileHistory(QStringLiteral("a.md"))),
                 (QStringList{QStringLiteral("Side edits a@a.md"), QStringLiteral("Base@a.md")}));
    }

    void detachedHead()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.write(QStringLiteral("a.md"), "a\n");
        const QString id = git.commit(QStringLiteral("One"), {QStringLiteral("a.md")}, kStart);
        QVERIFY(git.detach(id));
        QCOMPARE(GitRepository::discover(dir.path())->branch(), QStringLiteral("detached at %1").arg(id.left(7)));
    }
};

QTEST_GUILESS_MAIN(TestGitRepository)
#include "test_git_repository.moc"
