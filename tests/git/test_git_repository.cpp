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

    void blameNamesTheCommitOfEachLine()
    {
        QTemporaryDir dir;
        GitFixture git(dir.path());
        git.write(QStringLiteral("a.md"), "one\ntwo\nthree\n");
        const QString first = git.commit(QStringLiteral("Three lines"), {QStringLiteral("a.md")}, kStart);
        git.write(QStringLiteral("a.md"), "one\nTWO\nthree\n");
        const QString second = git.commit(QStringLiteral("Shout two"), {QStringLiteral("a.md")}, kStart + kDay);

        const auto repo = GitRepository::discover(dir.path());
        // The buffer: line 2 as committed, and a new line 3.
        const auto lines = repo->blame(QStringLiteral("a.md"), "one\nTWO\nunsaved\nthree\n");
        QVERIFY2(lines, lines ? "" : qPrintable(lines.error()));
        QCOMPARE(lines->size(), 4);
        QCOMPARE(lines->at(0).id, first);
        QCOMPARE(lines->at(0).summary, QStringLiteral("Three lines"));
        QCOMPARE(lines->at(1).id, second);
        QCOMPARE(lines->at(1).author, QStringLiteral("Ada Lovelace"));
        QCOMPARE(lines->at(1).time.toSecsSinceEpoch(), kStart + kDay);
        QVERIFY(lines->at(2).id.isEmpty()); // not committed
        QCOMPARE(lines->at(3).id, first);
        // Losing the final newline changes the last line, as git blame says too.
        const auto unterminated = repo->blame(QStringLiteral("a.md"), "one\nTWO\nthree");
        QCOMPARE(unterminated->size(), 3);
        QVERIFY(unterminated->at(2).id.isEmpty());

        // A file HEAD lacks: every line uncommitted.
        const auto fresh = repo->blame(QStringLiteral("new.md"), "a\nb\n");
        QVERIFY(fresh);
        QCOMPARE(fresh->size(), 2);
        QVERIFY(fresh->at(0).id.isEmpty() && fresh->at(1).id.isEmpty());
    }

    void diffListsTheWholeFile()
    {
        using K = DiffLine::Kind;
        const auto lines = GitRepository::diff("one\ntwo\nthree\nfour\n", "one\n2\nthree\nfour\nfive");
        QVERIFY(lines);
        QStringList brief;
        for (const DiffLine& line : *lines)
            brief.append(QStringLiteral("%1%2:%3 %4").arg(line.kind == K::Added ? '+' : line.kind == K::Removed ? '-' : ' ')
                             .arg(line.oldLine).arg(line.newLine).arg(line.text));
        QCOMPARE(brief, (QStringList{QStringLiteral(" 1:1 one"), QStringLiteral("-2:0 two"), QStringLiteral("+0:2 2"),
                                     QStringLiteral(" 3:3 three"), QStringLiteral(" 4:4 four"), QStringLiteral("+0:5 five")}));
        const auto same = GitRepository::diff("a\nb\n", "a\nb\n");
        QCOMPARE(same->size(), 2);
        QVERIFY(same->at(1).kind == K::Context && same->at(1).newLine == 2);
        const auto fromNothing = GitRepository::diff(QByteArray(), "new\n");
        QCOMPARE(fromNothing->size(), 1);
        QVERIFY(fromNothing->at(0).kind == K::Added);
        QVERIFY(GitRepository::diff("x\n", QByteArray())->at(0).kind == K::Removed);
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
