#include "parser/RegisterParser.hpp"

#include <QFile>
#include <QTest>

using coeus::RegisterEntry;
using coeus::RegisterParser;

namespace {

QString field(const RegisterEntry& entry, const QString& name)
{
    for (const coeus::RegisterField& f : entry.fields) {
        if (f.name == name)
            return f.value;
    }
    return QStringLiteral("<absent>");
}

QString readSource(const char* relative)
{
    QFile file(QStringLiteral(COEUS_SOURCE_DIR "/") + QLatin1String(relative));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class TestRegisterParser : public QObject {
    Q_OBJECT

private slots:
    void parsesEntriesAndFields()
    {
        const auto entries = RegisterParser::parse(QStringLiteral(
            "# Decisions\n\n"
            "### D-005 Vault is a plain folder\n"
            "**Decided:** 2026-09-03\n"
            "**Status:** Accepted\n"
            "**Related:** F-002, F-011, AV-001 (drift), D-5, AV-1\n\n"
            "**Context.** The author's notes already live in Git\n"
            "and are edited by other tools.\n\n"
            "**Options.**\n"
            "- **A. Database.** Rejected.\n\n"
            "**Decision.** Option B.\n\n"
            "### BUG-001: Something broke\n"
            "**Status:** open\n"
            "**Related decisions.** D-005, D-0013.\n\n"
            "## Fixed\n\n"
            "Not part of any entry. **Status:** ignored\n"));
        QCOMPARE(entries.size(), 2);
        const RegisterEntry& d = entries[0];
        QCOMPARE(d.ident, QStringLiteral("D-005"));
        QCOMPARE(d.kind, QStringLiteral("D"));
        QCOMPARE(d.number, 5);
        QCOMPARE(d.title, QStringLiteral("Vault is a plain folder"));
        QCOMPARE(d.line, 3);
        QCOMPARE(d.status, QStringLiteral("Accepted"));
        QCOMPARE(field(d, QStringLiteral("Context")), QStringLiteral("The author's notes already live in Git\nand are edited by other tools."));
        QCOMPARE(field(d, QStringLiteral("Options")), QStringLiteral("- **A. Database.** Rejected."));
        QCOMPARE(field(d, QStringLiteral("Decision")), QStringLiteral("Option B."));
        // Self-citation dropped, idents made canonical, duplicates once.
        QCOMPARE(d.references, (QStringList{QStringLiteral("F-002"), QStringLiteral("F-011"), QStringLiteral("AV-001")}));

        const RegisterEntry& bug = entries[1];
        QCOMPARE(bug.ident, QStringLiteral("BUG-001"));
        QCOMPARE(bug.title, QStringLiteral("Something broke"));
        QCOMPARE(bug.references, (QStringList{QStringLiteral("D-005"), QStringLiteral("D-013")}));
    }

    void recordsLinesAndMentions()
    {
        const auto entries = RegisterParser::parse(QStringLiteral(
            "### IMP-002: Faster scans, after BUG-004\n"      // 1
            "**Status:** suggested\n"                          // 2
            "**Trade-offs.** Memory for speed; see D-007.\n"   // 3
            "**Related.**\n"                                   // 4
            "- BUG-004 (the slow scan)\n"                      // 5
            "- D-007, IMP-002\n"                               // 6
            "\n"
            "Prose names F-1, F-010 and D-007 again.\n"));     // 8
        QCOMPARE(entries.size(), 1);
        const RegisterEntry& imp = entries[0];
        QCOMPARE(field(imp, QStringLiteral("Trade-offs")), QStringLiteral("Memory for speed; see D-007.")); // hyphenated name
        QCOMPARE(imp.fields[0].line, 2);
        QCOMPARE(imp.fields[2].line, 4);
        QCOMPARE(imp.fields[2].valueLine, 5); // a bare **Related.** starts its value below
        QCOMPARE(imp.references, (QStringList{QStringLiteral("BUG-004"), QStringLiteral("D-007")}));
        QCOMPARE(imp.referenceLines, (QList<int>{5, 6}));
        // Mentions: anywhere, title included, three digits or more, first line each, never itself.
        const QList<QPair<QString, int>> mentions{{QStringLiteral("BUG-004"), 1}, {QStringLiteral("D-007"), 3},
                                                  {QStringLiteral("F-010"), 8}};
        QCOMPARE(imp.mentions, mentions);
    }

    void ignoresCodeAndShortIds()
    {
        const auto entries = RegisterParser::parse(QStringLiteral(
            "```\n### D-001 Inside a fence\n```\n"
            "### D-01 Two digits are not an entry\n"
            "### F-1234 Four digits are\n"));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].ident, QStringLiteral("F-1234"));
    }

    void statusHeader()
    {
        const auto header = RegisterParser::statusHeader(QStringLiteral(
            "> **Status:** Active\n> **Provenance:** Shane\n> **Last reviewed:** 2026-10-13\n> **Why this status:** Building.\n\n# Coeus\n"));
        QVERIFY(header.has_value());
        QCOMPARE(header->status, QStringLiteral("Active"));
        QCOMPARE(header->lastReviewed, QStringLiteral("2026-10-13"));
        QVERIFY(!RegisterParser::statusHeader(QStringLiteral(
            "> **Provenance:** x\n> **Status:** Active\n> **Last reviewed:** x\n> **Why this status:** x\n"))); // order
        QVERIFY(!RegisterParser::statusHeader(QStringLiteral("# Just a README\n")));
        QVERIFY(RegisterParser::statusHeader(QStringLiteral(
            "> **Status:** A\r\n> **Provenance:** B\r\n> **Last reviewed:** C\r\n> **Why this status:** D\r\n")));
    }

    void fileNamesAndCanonicalIds()
    {
        QVERIFY(RegisterParser::isRegisterFile(QStringLiteral("ATTACK_VECTORS.md")));
        QVERIFY(!RegisterParser::isRegisterFile(QStringLiteral("decisions.md")));
        QCOMPARE(RegisterParser::kindOfFile(QStringLiteral("IMPROVEMENTS.md")), QStringLiteral("IMP"));
        QCOMPARE(RegisterParser::canonical(QStringLiteral("AV-7")), QStringLiteral("AV-007"));
        QCOMPARE(RegisterParser::canonical(QStringLiteral("x-7")), QString());
    }

    void parsesCoeusOwnRegisters()
    {
        // The standard's real documents: every entry heading must parse (AV-008).
        const QString decisions = readSource("DECISIONS.md");
        QVERIFY(!decisions.isEmpty());
        const auto entries = RegisterParser::parse(decisions);
        QCOMPARE(entries.size(), qsizetype(decisions.count(QStringLiteral("\n### D-"))));
        for (const RegisterEntry& entry : entries)
            QVERIFY2(!entry.status.isEmpty(), qPrintable(entry.ident + QStringLiteral(" has no status")));
        const auto features = RegisterParser::parse(readSource("FEATURES.md"));
        QVERIFY(features.size() >= 35);
    }
};

QTEST_GUILESS_MAIN(TestRegisterParser)
#include "test_register_parser.moc"
