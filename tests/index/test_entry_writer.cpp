// F-033 (append_entry): writing a register entry, and placing it, so that
// the parser reads back what was meant and the lint finds no error.

#include "parser/RegisterParser.hpp"
#include "registers/EntryWriter.hpp"
#include "registers/Lint.hpp"

#include <QTest>

using namespace coeus;

namespace {

const QDate kToday(2026, 10, 14);

QString field(const RegisterEntry& entry, const QString& name)
{
    for (const RegisterField& f : entry.fields) {
        if (f.name == name)
            return f.value;
    }
    return QStringLiteral("<absent>");
}

// Errors the lint would report for `text` as one project's register.
QStringList lintErrors(const QString& text)
{
    QList<Lint::Entry> entries;
    std::int64_t id = 0;
    for (const RegisterEntry& parsed : RegisterParser::parse(text)) {
        Lint::Entry entry;
        entry.id = ++id;
        entry.note = 1;
        entry.notePath = QStringLiteral("REG.md");
        entry.project = 1;
        entry.kind = parsed.kind;
        entry.ident = parsed.ident;
        entry.line = parsed.line;
        entry.fields = parsed.fields;
        entries.append(entry);
    }
    QStringList errors;
    for (const Lint::Problem& problem : Lint::check(entries, {})) {
        if (problem.level == QLatin1String("error"))
            errors.append(problem.rule + QLatin1Char(' ') + problem.message);
    }
    return errors;
}

} // namespace

class TestEntryWriter : public QObject {
    Q_OBJECT

private slots:
    void aDecisionReadsBackAsWritten()
    {
        const auto text = EntryWriter::render(
            {QStringLiteral("D"), QStringLiteral("D-021"), QStringLiteral("Push with git"),
             {{QStringLiteral("reversal conditions"), QStringLiteral("If libgit2 learns credential helpers.")},
              {QStringLiteral("Context"), QStringLiteral("Remotes are HTTPS.")},
              {QStringLiteral("Options"), QStringLiteral("- **A. libgit2.** Rejected.\n- **B. git.** Chosen.")},
              {QStringLiteral("Related"), QStringLiteral("F-022, AV-005")},
              {QStringLiteral("Decision"), QStringLiteral("Option B.")}}},
            kToday);
        QVERIFY2(text, text ? "" : qPrintable(text.error()));
        QCOMPARE(*text, QStringLiteral(
            "### D-021 Push with git\n**Decided:** 2026-10-14\n**Recorded:** 2026-10-14\n**Status:** Proposed\n**Related:** F-022, AV-005\n"
            "\n**Context.** Remotes are HTTPS.\n"
            "\n**Options.**\n- **A. libgit2.** Rejected.\n- **B. git.** Chosen.\n"
            "\n**Decision.** Option B.\n"
            "\n**Reversal conditions.** If libgit2 learns credential helpers.\n"));
        const auto parsed = RegisterParser::parse(*text);
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed[0].ident, QStringLiteral("D-021"));
        QCOMPARE(parsed[0].status, QStringLiteral("Proposed"));
        QCOMPARE(field(parsed[0], QStringLiteral("Options")), QStringLiteral("- **A. libgit2.** Rejected.\n- **B. git.** Chosen."));
        QCOMPARE(parsed[0].references, (QStringList{QStringLiteral("F-022"), QStringLiteral("AV-005")}));
        QCOMPARE(lintErrors(*text), QStringList());
    }

    void bugsAndImprovementsAreCompact()
    {
        const auto bug = EntryWriter::render({QStringLiteral("BUG"), QStringLiteral("BUG-008"), QStringLiteral("Toast hides the tab"),
                                              {{QStringLiteral("Severity"), QStringLiteral("low")},
                                               {QStringLiteral("Description"), QStringLiteral("It overlaps.")}}},
                                             kToday);
        QCOMPARE(*bug, QStringLiteral("### BUG-008: Toast hides the tab\n**Status:** open\n**Severity:** low\n**Found:** 2026-10-14\n"
                                      "**Description.** It overlaps.\n"));
        const auto imp = EntryWriter::render({QStringLiteral("IMP"), QStringLiteral("IMP-006"), QStringLiteral("Faster"),
                                              {{QStringLiteral("Trade-offs"), QStringLiteral("Memory.")},
                                               {QStringLiteral("Status"), QStringLiteral("applied (2026-10-14)")},
                                               {QStringLiteral("Effort"), QStringLiteral("small")},
                                               {QStringLiteral("Seen in"), QStringLiteral("Pontus")}}},
                                             kToday);
        QCOMPARE(*imp, QStringLiteral("### IMP-006: Faster\n**Status:** applied (2026-10-14)\n**Found:** 2026-10-14\n**Effort:** small\n"
                                      "**Trade-offs.** Memory.\n**Seen in.** Pontus\n")); // unknown fields last, as paragraphs
        QCOMPARE(lintErrors(*bug + QLatin1Char('\n') + *imp), QStringList());
    }

    void whatTheLintWouldRejectIsRefused()
    {
        const auto refused = [](const EntryWriter::Entry& entry) { return !EntryWriter::render(entry, kToday); };
        const QString title = QStringLiteral("T");
        QVERIFY(refused({QStringLiteral("X"), QStringLiteral("X-001"), title, {}}));                                       // kind
        QVERIFY(refused({QStringLiteral("D"), QStringLiteral("F-001"), title, {}}));                                       // ident of another kind
        QVERIFY(refused({QStringLiteral("F"), QStringLiteral("F-001"), QStringLiteral(" "), {}}));                         // title
        QVERIFY(refused({QStringLiteral("D"), QStringLiteral("D-001"), title, {}}));                                       // L5
        QVERIFY(refused({QStringLiteral("AV"), QStringLiteral("AV-001"), title, {}}));                                     // L7
        QVERIFY(refused({QStringLiteral("IMP"), QStringLiteral("IMP-001"), title, {{QStringLiteral("Trade-offs"), QString()}}})); // L6, empty
        const auto vocabulary = EntryWriter::render({QStringLiteral("BUG"), QStringLiteral("BUG-001"), title,
                                                     {{QStringLiteral("Severity"), QStringLiteral("Medium")}}}, kToday);
        QVERIFY(!vocabulary);
        QVERIFY(vocabulary.error().contains(QStringLiteral("low, medium, high"))); // L3, case-sensitive
        QVERIFY(refused({QStringLiteral("F"), QStringLiteral("F-001"), title, {{QStringLiteral("Status"), QStringLiteral("Complete\nreally")}}}));
        QVERIFY(refused({QStringLiteral("F"), QStringLiteral("F-001"), title, {{QStringLiteral("Notes"), QStringLiteral("a")}, {QStringLiteral("notes"), QStringLiteral("b")}}}));
        QVERIFY(refused({QStringLiteral("F"), QStringLiteral("F-001"), title, {{QStringLiteral("**Sneaky**"), QStringLiteral("a")}}}));
    }

    void placedAtTheEndOrInItsSection()
    {
        const QString entry = QStringLiteral("### D-003 Third\n**Status:** Proposed\n");
        auto placed = EntryWriter::place(QStringLiteral("# Decisions\n\n### D-001 One\n\n### D-002 Two\n\n\n"), QStringLiteral("D"),
                                         QStringLiteral("Proposed"), entry);
        QCOMPARE(placed.text, QStringLiteral("# Decisions\n\n### D-001 One\n\n### D-002 Two\n\n### D-003 Third\n**Status:** Proposed\n"));
        QCOMPARE(placed.line, 7);

        // A fixed bug goes under Fixed; the section lists newest first, so at its top.
        const QString bugs = QStringLiteral("# Bugs\n\n## Open\n\n_None._\n\n## Fixed\n\n### BUG-002: B\nx\n\n### BUG-001: A\ny\n\n## Deferred\n");
        placed = EntryWriter::place(bugs, QStringLiteral("BUG"), QStringLiteral("fixed (today)"), QStringLiteral("### BUG-003: C\nz\n"));
        QCOMPARE(placed.text, QStringLiteral("# Bugs\n\n## Open\n\n_None._\n\n## Fixed\n\n### BUG-003: C\nz\n\n### BUG-002: B\nx\n\n"
                                             "### BUG-001: A\ny\n\n## Deferred\n"));
        QCOMPARE(placed.line, 9);
        // An open one replaces "_None._".
        placed = EntryWriter::place(bugs, QStringLiteral("BUG"), QStringLiteral("open"), QStringLiteral("### BUG-003: C\nz\n"));
        QVERIFY(placed.text.startsWith(QStringLiteral("# Bugs\n\n## Open\n\n### BUG-003: C\nz\n\n## Fixed\n")));
        QCOMPARE(placed.line, 5);
        // Oldest first: at the section's end.
        placed = EntryWriter::place(QStringLiteral("## Applied\n\n### IMP-001: A\n\n### IMP-002: B\n\n## Declined\n"), QStringLiteral("IMP"),
                                    QStringLiteral("applied"), QStringLiteral("### IMP-003: C\n"));
        QCOMPARE(placed.text, QStringLiteral("## Applied\n\n### IMP-001: A\n\n### IMP-002: B\n\n### IMP-003: C\n\n## Declined\n"));
        // No such section: the end of the file.
        placed = EntryWriter::place(QStringLiteral("# Bugs\n"), QStringLiteral("BUG"), QStringLiteral("open"), QStringLiteral("### BUG-001: A\n"));
        QCOMPARE(placed.text, QStringLiteral("# Bugs\n\n### BUG-001: A\n"));
        QCOMPARE(placed.line, 3);
    }
};

QTEST_GUILESS_MAIN(TestEntryWriter)
#include "test_entry_writer.moc"
