// The register lint (F-016): each rule, pure, then through the index.

#include "IndexDump.hpp"

#include "index/IndexQueries.hpp"
#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"
#include "index/LintResolver.hpp"
#include "registers/Lint.hpp"
#include "registers/Vocabulary.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

Lint::Entry entry(std::int64_t id, const QString& ident, QList<RegisterField> fields = {},
                  QList<Lint::Citation> citations = {}, QList<QPair<QString, int>> mentions = {})
{
    Lint::Entry e;
    e.id = id;
    e.note = 10 + id;
    e.notePath = QStringLiteral("REG.md");
    e.project = 1;
    e.ident = ident;
    e.kind = ident.section(QLatin1Char('-'), 0, 0);
    e.line = int(id) * 10;
    e.fields = fields;
    e.citations = citations;
    e.mentions = mentions;
    return e;
}

RegisterField field(const QString& name, const QString& value, int line = 2)
{
    return {name, value, line, line};
}

// "L1@12 D-001" for each problem, to compare briefly.
QStringList brief(const QList<Lint::Problem>& problems)
{
    QStringList out;
    for (const Lint::Problem& p : problems)
        out.append(QStringLiteral("%1@%2 %3").arg(p.rule).arg(p.line).arg(p.subject));
    return out;
}

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** Test\n> **Last reviewed:** 2026-10-01\n"
                           "> **Why this status:** Testing.\n\n# Project\n";

} // namespace

class TestLint : public QObject {
    Q_OBJECT

private slots:
    void vocabularyTermsTakeQualifiers()
    {
        const auto term = [](const char* kind, const char* field, const char* value) {
            return Vocabulary::registerTerm(QLatin1String(kind), QLatin1String(field), QString::fromUtf8(value));
        };
        QCOMPARE(term("BUG", "Status", "fixed (2026-10-05)"), std::optional<QString>(QStringLiteral("fixed")));
        QCOMPARE(term("F", "Status", "Complete (Phase 2, 2026-10-13): built"), std::optional<QString>(QStringLiteral("Complete")));
        QCOMPARE(term("F", "Status", "Not started"), std::optional<QString>(QStringLiteral("Not started")));
        QCOMPARE(term("F", "Priority", "Won't (this version)"), std::optional<QString>(QStringLiteral("Won't")));
        QCOMPARE(term("IMP", "Status", "applied 2026-09-23"), std::optional<QString>(QStringLiteral("applied")));
        QCOMPARE(term("D", "Status", "Superseded by D-19 (2026-10-06): moved"), std::optional<QString>(QStringLiteral("Superseded")));
        QVERIFY(!term("D", "Status", "Superseded"));    // by what?
        QVERIFY(!term("F", "Status", "Completed"));     // a term must end at a non-word character
        QVERIFY(!term("BUG", "Severity", "Medium"));    // case-sensitive
        QVERIFY(!term("F", "Status", "Done 2026-10-10"));
        QVERIFY(!term("BUG", "Severity", ""));
        QCOMPARE(term("F", "Notes", "anything"), std::optional<QString>(QStringLiteral("anything"))); // no vocabulary
        QVERIFY(Vocabulary::isStaleStatus(QStringLiteral("declined")));
        QVERIFY(!Vocabulary::isStaleStatus(QStringLiteral("Accepted")));
    }

    void citationRules()
    {
        // D-001 cites a missing D-009 (L1), a withdrawn F-002 (L2), and F-003,
        // which does not cite back (L4). F-002 cites D-001 back, so no L4 there.
        const QList<Lint::Entry> entries{
            entry(1, QStringLiteral("D-001"), {field(QStringLiteral("Status"), QStringLiteral("Accepted")), field(QStringLiteral("Reversal conditions"), QStringLiteral("x"))},
                  {{QStringLiteral("D-009"), 12, 0}, {QStringLiteral("F-002"), 12, 2}, {QStringLiteral("F-003"), 13, 3}},
                  {{QStringLiteral("D-009"), 12}, {QStringLiteral("F-002"), 12}, {QStringLiteral("F-003"), 13}}),
            entry(2, QStringLiteral("F-002"), {field(QStringLiteral("Status"), QStringLiteral("Withdrawn (absorbed)"))}, {},
                  {{QStringLiteral("D-001"), 21}}),
            entry(3, QStringLiteral("F-003"), {field(QStringLiteral("Status"), QStringLiteral("Complete"))}),
        };
        QCOMPARE(brief(Lint::check(entries, {})),
                 (QStringList{QStringLiteral("L1@12 D-009"), QStringLiteral("L2@12 F-002"), QStringLiteral("L4@13 F-003")}));
    }

    void backReferencesFollowTheStandardsPairs()
    {
        QVERIFY(Lint::needsBackReference(QStringLiteral("D"), QStringLiteral("AV")));
        QVERIFY(Lint::needsBackReference(QStringLiteral("BUG"), QStringLiteral("BUG")));
        QVERIFY(!Lint::needsBackReference(QStringLiteral("D"), QStringLiteral("D"))); // a decision citing an older one
        QVERIFY(!Lint::needsBackReference(QStringLiteral("F"), QStringLiteral("AV")));
        const QList<Lint::Entry> entries{
            entry(1, QStringLiteral("D-002"), {field(QStringLiteral("Reversal conditions"), QStringLiteral("x"))}, {},
                  {{QStringLiteral("D-001"), 11}}),
            entry(2, QStringLiteral("D-001"), {field(QStringLiteral("Reversal conditions"), QStringLiteral("x"))}),
        };
        QVERIFY(Lint::check(entries, {}).isEmpty());
    }

    void supersededByTheCiterIsNotStale()
    {
        const QList<Lint::Entry> entries{
            entry(1, QStringLiteral("D-032"), {field(QStringLiteral("Reversal conditions"), QStringLiteral("x"))},
                  {{QStringLiteral("D-017"), 11, 2}}),
            entry(2, QStringLiteral("D-017"), {field(QStringLiteral("Status"), QStringLiteral("Superseded by D-32")),
                                               field(QStringLiteral("Reversal conditions"), QStringLiteral("x"))}),
            entry(3, QStringLiteral("AV-001"), {field(QStringLiteral("Detection"), QStringLiteral("x"))},
                  {{QStringLiteral("D-017"), 31, 2}}),
        };
        QCOMPARE(brief(Lint::check(entries, {})), (QStringList{QStringLiteral("L2@31 D-017")}));
    }

    void vocabularyAndRequiredFields()
    {
        const QList<Lint::Entry> entries{
            entry(1, QStringLiteral("BUG-001"), {field(QStringLiteral("Status"), QStringLiteral("fixed (2026-10-05)"), 11),
                                                 field(QStringLiteral("Severity"), QStringLiteral("Medium"), 12)}),
            entry(2, QStringLiteral("D-001"), {field(QStringLiteral("Status"), QStringLiteral(""), 21)}),
            entry(3, QStringLiteral("IMP-001"), {field(QStringLiteral("Trade-offs"), QStringLiteral("Some"))}),
            entry(4, QStringLiteral("IMP-002"), {field(QStringLiteral("Status"), QStringLiteral("suggested"))}),
            entry(5, QStringLiteral("AV-001")),
            entry(6, QStringLiteral("C-001"), {field(QStringLiteral("Domain"), QStringLiteral("Empirical")),
                                               field(QStringLiteral("Reversal conditions"), QStringLiteral("x"))}),
        };
        const QList<Lint::Project> projects{{1, 100, QStringLiteral("Active (since 2026-09)"), QStringLiteral("2026-10-01")},
                                            {2, 101, QStringLiteral("Paused"), QStringLiteral("2026-10-01")}};
        QCOMPARE(brief(Lint::check(entries, projects)),
                 (QStringList{QStringLiteral("L3@12 Medium"), QStringLiteral("L5@20 D-001"), QStringLiteral("L3@21 "),
                              QStringLiteral("L6@40 IMP-002"), QStringLiteral("L7@50 AV-001"), QStringLiteral("L3@1 Paused")}));
    }

    void duplicatesAndEntriesOutsideProjects()
    {
        Lint::Entry first = entry(1, QStringLiteral("F-001"), {field(QStringLiteral("Status"), QStringLiteral("Complete"))});
        Lint::Entry again = entry(2, QStringLiteral("F-001"), {field(QStringLiteral("Status"), QStringLiteral("Complete"))});
        again.notePath = QStringLiteral("A.md"); // sorts first by path, so it keeps the ID
        Lint::Entry loose = entry(3, QStringLiteral("F-002"), {field(QStringLiteral("Status"), QStringLiteral("Nonsense"))},
                                  {{QStringLiteral("D-404"), 31, 0}});
        loose.project = 0;
        const auto problems = Lint::check({first, again, loose}, {});
        QCOMPARE(brief(problems), (QStringList{QStringLiteral("L9@10 F-001")}));
        QCOMPARE(problems[0].note, first.note);
        QVERIFY(problems[0].message.contains(QStringLiteral("A.md:20")));
    }

    void staleReview()
    {
        const Lint::Project project{1, 100, QStringLiteral("Active"), QStringLiteral("2026-10-01")};
        QVERIFY(!Lint::staleReview(project, QDate(2026, 10, 15), 14)); // 14 days: not yet
        const auto stale = Lint::staleReview(project, QDate(2026, 10, 16), 14);
        QVERIFY(stale);
        QCOMPARE(stale->rule, QStringLiteral("L8"));
        QCOMPARE(stale->level, QStringLiteral("info"));
        QCOMPARE(stale->line, 3);
        QVERIFY(stale->message.contains(QStringLiteral("15 days")));
        QVERIFY(Lint::staleReview({1, 100, QStringLiteral("Active"), QStringLiteral("last week")}, QDate(2026, 10, 2), 14));
    }

    void throughTheIndex()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("p/README.md")), kHeader);
        writeFile(vault.filePath(QStringLiteral("p/DECISIONS.md")),
                  "# Decisions\n\n### D-001 First\n**Status:** Accepted\n**Related:** AV-001, F-404\n\n"
                  "**Reversal conditions.** None.\n");
        writeFile(vault.filePath(QStringLiteral("p/ATTACK_VECTORS.md")),
                  "### AV-001 Drift\n**Severity:** Major\n**Detection.** A test.\n");
        writeFile(vault.filePath(QStringLiteral("loose/BUGS.md")), "### BUG-001: Outside\n**Status:** whatever\n");
        const QString indexPath = vault.filePath(QStringLiteral("index.db"));
        auto store = IndexStore::open(indexPath);
        QVERIFY(store);
        Indexer indexer(*store);
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        QVERIFY(store->putRoot(root));
        QVERIFY(indexer.scan(root));

        const auto problems = [&] {
            QStringList out;
            const QList<ProblemRef> found = IndexQueries::problems(indexPath, QDate(2026, 10, 13), 14).value_or(QList<ProblemRef>());
            for (const ProblemRef& p : found)
                out.append(QStringLiteral("%1 %2:%3 %4").arg(p.rule, p.notePath.mid(vault.path().size() + 1)).arg(p.line).arg(p.subject));
            return out;
        };
        QCOMPARE(problems(), (QStringList{QStringLiteral("L1 p/DECISIONS.md:5 F-404"), QStringLiteral("L4 p/DECISIONS.md:5 AV-001")}));

        // Fixing the file on disk fixes the lint on the next update, and rewrites only what changed.
        writeFile(vault.filePath(QStringLiteral("p/ATTACK_VECTORS.md")),
                  "### AV-001 Drift\n**Severity:** Major\n**Detection.** A test.\n**Related decisions:** D-001\n");
        QVERIFY(indexer.update(root, {QStringLiteral("p/ATTACK_VECTORS.md")}));
        QCOMPARE(problems(), (QStringList{QStringLiteral("L1 p/DECISIONS.md:5 F-404")}));
        const auto again = LintResolver::resolveAll(store->database());
        QVERIFY(again);
        QCOMPARE(again->problems, 1);
        QCOMPARE(again->changed, 0);

        // L8 is evaluated when read, against the date asked about.
        QCOMPARE(IndexQueries::problems(indexPath, QDate(2026, 10, 30), 14)->size(), 2);
        QCOMPARE(IndexQueries::problems(indexPath, QDate(2026, 10, 30), 60)->size(), 1);
    }

    void coeusOwnRegistersHaveNoErrors()
    {
        // The standard's real documents (AV-008): a parser that drifts from them
        // shows up here as errors, as **Trade-offs:** did before hyphens were allowed.
        QTemporaryDir vault;
        for (const char* name : {"README.md", "FEATURES.md", "DECISIONS.md", "ATTACK_VECTORS.md", "BUGS.md", "IMPROVEMENTS.md"}) {
            QFile source(QStringLiteral(COEUS_SOURCE_DIR "/") + QLatin1String(name));
            QVERIFY(source.open(QIODevice::ReadOnly));
            writeFile(vault.filePath(QLatin1String(name)), source.readAll());
        }
        auto store = IndexStore::open(QStringLiteral(":memory:"));
        QVERIFY(store);
        Indexer indexer(*store);
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        QVERIFY(store->putRoot(root));
        const auto scanned = indexer.scan(root);
        QVERIFY2(scanned, scanned ? "" : qPrintable(scanned.error()));
        auto rows = store->database().prepare("SELECT l.rule, n.path, l.line, l.message FROM lint l JOIN notes n ON n.id = l.note "
                                              "WHERE l.level = 'error'");
        QStringList errors;
        while (rows->step().value_or(false))
            errors.append(QStringLiteral("%1 %2:%3 %4").arg(rows->text(0), rows->text(1), rows->text(2), rows->text(3)));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        auto entries = store->database().prepare("SELECT count(*) FROM registers WHERE project_id IS NOT NULL");
        QVERIFY(entries->step().value_or(false));
        QVERIFY(entries->int64(0) > 50); // the README made it a project, so everything was linted
    }
};

QTEST_GUILESS_MAIN(TestLint)
#include "test_lint.moc"
