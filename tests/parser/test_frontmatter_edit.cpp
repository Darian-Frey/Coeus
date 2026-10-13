#include "parser/Frontmatter.hpp"
#include "parser/NoteParser.hpp"

#include <QTest>

using namespace coeus;

namespace {

QString set(const QString& text, const QString& key, const QString& yaml)
{
    return Frontmatter::apply(text, Frontmatter::setValue(text, key, yaml));
}

// The value YAML gives back for `key` after an edit, and its type.
std::pair<QStringList, PropertyType> readBack(const QString& text, const QString& key)
{
    const ParsedNote note = NoteParser::parse(text.toUtf8());
    for (const Property& p : note.properties) {
        if (p.key == key)
            return {p.values, p.type};
    }
    return {{QStringLiteral("<absent>")}, PropertyType::Text};
}

} // namespace

class TestFrontmatterEdit : public QObject {
    Q_OBJECT

private slots:
    void replacesOnlyTheKeysLine()
    {
        const QString before = QStringLiteral("---\n# a comment\nstatus: Dormant  # was Active\ntags: [a]\n---\n# Body\n");
        const QString after = set(before, QStringLiteral("status"), QStringLiteral("Active"));
        QCOMPARE(after, QStringLiteral("---\n# a comment\nstatus: Active\ntags: [a]\n---\n# Body\n"));
    }

    void blockListBecomesOneFlowLine()
    {
        const QString before = QStringLiteral("---\naliases:\n  - One\n  - Two\ntags:\n- x\n- y\nkept: yes\n---\n");
        QString after = set(before, QStringLiteral("aliases"), Frontmatter::yamlList({QStringLiteral("Uno")}));
        after = set(after, QStringLiteral("tags"), Frontmatter::yamlList({QStringLiteral("x"), QStringLiteral("z")}));
        QCOMPARE(after, QStringLiteral("---\naliases: [Uno]\ntags: [x, z]\nkept: yes\n---\n"));
        QCOMPARE(readBack(after, QStringLiteral("tags")).first, (QStringList{QStringLiteral("x"), QStringLiteral("z")}));
    }

    void addsMissingKeyAndCreatesFrontmatter()
    {
        QCOMPARE(set(QStringLiteral("---\na: 1\n---\nBody\n"), QStringLiteral("b"), QStringLiteral("2")),
                 QStringLiteral("---\na: 1\nb: 2\n---\nBody\n"));
        QCOMPARE(set(QStringLiteral("# Just a body\n"), QStringLiteral("status"), QStringLiteral("Active")),
                 QStringLiteral("---\nstatus: Active\n---\n# Just a body\n"));
        // An unclosed block is not frontmatter: a new one goes on top.
        QVERIFY(set(QStringLiteral("---\nnot closed\n"), QStringLiteral("k"), QStringLiteral("v"))
                    .startsWith(QStringLiteral("---\nk: v\n---\n---\n")));
    }

    void removesKeyWithContinuationLines()
    {
        const QString before = QStringLiteral("---\nfirst: 1\nlist:\n  - a\n  - b\nlast: 2\n---\n");
        const auto edit = Frontmatter::removeKey(before, QStringLiteral("list"));
        QVERIFY(edit.has_value());
        QCOMPARE(Frontmatter::apply(before, *edit), QStringLiteral("---\nfirst: 1\nlast: 2\n---\n"));
        QVERIFY(!Frontmatter::removeKey(before, QStringLiteral("absent")));
        QVERIFY(!Frontmatter::removeKey(QStringLiteral("no frontmatter"), QStringLiteral("x")));
    }

    void crlfAndQuotedKeys()
    {
        const QString before = QStringLiteral("---\r\n\"odd key\": 1\r\nother: 2\r\n---\r\n");
        const QString after = set(before, QStringLiteral("odd key"), QStringLiteral("3"));
        QCOMPARE(after, QStringLiteral("---\r\n\"odd key\": 3\r\nother: 2\r\n---\r\n"));
    }

    void scalarsReadBackAsWritten()
    {
        // Each value must come back as the same text, and as text.
        const QStringList values{QStringLiteral("plain words"), QStringLiteral("2026"), QStringLiteral("2026-10-13"),
                                 QStringLiteral("yes"), QStringLiteral("a: b"), QStringLiteral("# not a comment"),
                                 QStringLiteral("- dash"), QStringLiteral("say \"hi\""), QStringLiteral("back\\slash"),
                                 QStringLiteral(" padded "), QStringLiteral("Κοῖος"), QString()};
        for (const QString& value : values) {
            const QString yaml = Frontmatter::yamlScalar(value);
            const auto [read, type] = readBack(QStringLiteral("---\nk: %1\n---\n").arg(yaml), QStringLiteral("k"));
            QVERIFY2(read == QStringList{value} || (value.isEmpty() && read == QStringList{QString()}),
                     qPrintable(QStringLiteral("%1 -> %2 -> %3").arg(value, yaml, read.join(QLatin1Char('|')))));
            if (!value.isEmpty())
                QCOMPARE(type, PropertyType::Text);
        }
        QCOMPARE(Frontmatter::yamlScalar(QStringLiteral("plain words")), QStringLiteral("plain words"));
        QCOMPARE(Frontmatter::yamlScalar(QStringLiteral("2026")), QStringLiteral("\"2026\""));
        QCOMPARE(Frontmatter::yamlScalar(QStringLiteral("42"), PropertyType::Number), QStringLiteral("42"));
        QCOMPARE(Frontmatter::yamlScalar(QStringLiteral("2026-10-13"), PropertyType::Date), QStringLiteral("2026-10-13"));
    }

    void listsQuoteWhatFlowSyntaxNeeds()
    {
        const QString yaml = Frontmatter::yamlList({QStringLiteral("a"), QStringLiteral("b, c"), QStringLiteral("[d]")});
        const auto [read, type] = readBack(QStringLiteral("---\nk: %1\n---\n").arg(yaml), QStringLiteral("k"));
        QCOMPARE(type, PropertyType::List);
        QCOMPARE(read, (QStringList{QStringLiteral("a"), QStringLiteral("b, c"), QStringLiteral("[d]")}));
    }
};

QTEST_GUILESS_MAIN(TestFrontmatterEdit)
#include "test_frontmatter_edit.moc"
