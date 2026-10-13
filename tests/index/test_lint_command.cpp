// `coeus --lint` (IMP-004): the cases tools/check_xrefs.py was tested on,
// now against the register lint, plus exit status and --mentions.

#include "IndexDump.hpp"

#include "registers/LintCommand.hpp"

#include <git2.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

const QByteArray kReadme = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-13\n"
                           "> **Why this status:** T\n\n# Project\n";

struct Result {
    int status = 0;
    QStringList lines; // stdout
    QString summary;   // stderr
};

} // namespace

class TestLintCommand : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;

    void write(const char* name, const QByteArray& text) { writeFile(m_dir->filePath(QLatin1String(name)), text); }

    Result lint(LintCommand::Options options = {})
    {
        options.today = QDate(2026, 10, 13);
        QString out;
        QString err;
        QTextStream outStream(&out);
        QTextStream errStream(&err);
        Result result;
        result.status = LintCommand::run(m_dir->path(), options, outStream, errStream);
        result.lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        result.summary = err.trimmed();
        return result;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        write("README.md", kReadme);
    }

    void cleanBidirectionalPair()
    {
        write("DECISIONS.md", "### D-001 Choose\n**Status:** Accepted\n**Related:** AV-001\n**Reversal conditions.** None.\n");
        write("ATTACK_VECTORS.md", "### AV-001 Risk\n**Severity:** Major\n**Detection.** A test.\n**Related decisions.** D-001.\n");
        const Result result = lint();
        QCOMPARE(result.lines, QStringList());
        QCOMPARE(result.status, 0);
        QCOMPARE(result.summary, QStringLiteral("coeus --lint: 0 error(s), 0 warning(s), 0 info, in 1 project"));
    }

    void oneWayReferenceIsAWarningAndStrictFails()
    {
        write("DECISIONS.md", "### D-001 Choose\n**Related:** AV-001\n**Reversal conditions.** None.\n");
        write("ATTACK_VECTORS.md", "### AV-001 Risk\n**Detection.** A test.\n");
        QCOMPARE(lint().lines,
                 QStringList{QStringLiteral("DECISIONS.md:2: warning: D-001 cites AV-001, but AV-001 does not cite D-001 (L4)")});
        QCOMPARE(lint().status, 0);
        LintCommand::Options strict;
        strict.strict = true;
        QCOMPARE(lint(strict).status, 1);
        LintCommand::Options quiet;
        quiet.quiet = true;
        QCOMPARE(lint(quiet).lines, QStringList()); // errors only
    }

    void pairsOutsideTheRulesAreNotReported()
    {
        write("DECISIONS.md", "### D-001 One\n**Reversal conditions.** x\n\n### D-002 Two\n**Related:** D-001\n**Reversal conditions.** x\n");
        QCOMPARE(lint().lines, QStringList());
    }

    void undefinedMentionsAnywhereWithTheFlag()
    {
        write("DECISIONS.md", "### D-001 One\n**Reversal conditions.** x\n**Related:** D-404\n");
        write("docs/SPEC.md", "See D-001 and D-099.\n\n```\nident TEXT -- e.g. 'AV-999'\n```\nNot XD-001, nor D-001a, nor D-01.\n");
        write("other/README.md", kReadme);
        write("other/NOTES.md", "D-099 is defined nowhere, but this is another project's note: also reported.\n");
        write("loose.md", "Outside every project? No: the root is the project here.\n");
        QCOMPARE(lint().lines, QStringList{QStringLiteral("DECISIONS.md:3: error: D-001 cites D-404, which does not exist in this project (L1)")});
        LintCommand::Options mentions;
        mentions.mentions = true;
        const Result result = lint(mentions);
        // D-404 once (as L1, not again as a mention); fenced AV-999 and malformed IDs ignored;
        // the nested project checks its own entries, so D-001 would be undefined there too.
        QCOMPARE(result.lines, (QStringList{
            QStringLiteral("DECISIONS.md:3: error: D-001 cites D-404, which does not exist in this project (L1)"),
            QStringLiteral("docs/SPEC.md:1: error: D-099 is mentioned but no entry in this project defines it (mention)"),
            QStringLiteral("other/NOTES.md:1: error: D-099 is mentioned but no entry in this project defines it (mention)"),
        }));
        QCOMPARE(result.status, 1);
    }

    void duplicatesAreErrors()
    {
        write("BUGS.md", "### BUG-001: First\n**Status:** open\n**Severity:** low\n\n### BUG-001: Again\n**Status:** open\n**Severity:** low\n");
        const Result result = lint();
        QCOMPARE(result.lines, QStringList{QStringLiteral("BUGS.md:5: error: BUG-001 is already used at BUGS.md:1; IDs are never reused (AV-003) (L9)")});
        QCOMPARE(result.status, 1);
    }

    void entryEndsAtTheNextHeading()
    {
        write("DECISIONS.md", "### D-001 One\n**Reversal conditions.** x\n\n## Notes\nAV-001 here is not D-001's.\n");
        write("ATTACK_VECTORS.md", "### AV-001 Risk\n**Detection.** x\n");
        QCOMPARE(lint().lines, QStringList());
    }

    void bugRequiresABackReferenceFromImp()
    {
        write("BUGS.md", "### BUG-001: Broken\n**Notes.** See IMP-001.\n");
        write("IMPROVEMENTS.md", "### IMP-001: Better\n**Trade-offs.** None.\n**Notes.** Nothing.\n");
        QCOMPARE(lint().lines,
                 QStringList{QStringLiteral("BUGS.md:2: warning: BUG-001 cites IMP-001, but IMP-001 does not cite BUG-001 (L4)")});
    }

    void ignoredFilesAreNotLinted()
    {
        git_libgit2_init(); // GitIgnore reads the rules of the repository the folder is in
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_init(&repo, QFile::encodeName(m_dir->path()).constData(), 0), 0);
        git_repository_free(repo);
        git_libgit2_shutdown();
        write(".gitignore", "build/\n");
        write("build/_deps/DECISIONS.md", "### D-001 Third-party\n");
        QCOMPARE(lint().lines, QStringList());
    }

    void staleReviewIsInfoAndNeverFails()
    {
        write("README.md", QByteArray(kReadme).replace("2026-10-13", "2026-01-01"));
        const Result result = lint();
        QCOMPARE(result.lines.size(), 1);
        QVERIFY(result.lines[0].startsWith(QStringLiteral("README.md:3: info: Last reviewed 2026-01-01")));
        QCOMPARE(result.status, 0);
    }

    void nothingToLintOrNoFolder()
    {
        write("README.md", "# No status header\n");
        QCOMPARE(lint().status, 1);
        QVERIFY(lint().summary.contains(QStringLiteral("no project")));
        m_dir.reset(new QTemporaryDir);
        const QString gone = m_dir->path();
        m_dir->remove();
        QString out;
        QString err;
        QTextStream o(&out);
        QTextStream e(&err);
        QCOMPARE(LintCommand::run(gone, {}, o, e), 2);
    }
};

QTEST_GUILESS_MAIN(TestLintCommand)
#include "test_lint_command.moc"
