#include "parser/NoteParser.hpp"

#include <QTest>

using namespace coeus;

namespace {

ParsedNote parse(const char* text)
{
    return NoteParser::parse(QByteArrayView(text));
}

QStringList tagNames(const ParsedNote& note)
{
    QStringList names;
    for (const Tag& tag : note.tags)
        names.append(tag.name);
    return names;
}

const Property* findProperty(const ParsedNote& note, const QString& key)
{
    for (const Property& p : note.properties) {
        if (p.key == key)
            return &p;
    }
    return nullptr;
}

} // namespace

class TestNoteParser : public QObject {
    Q_OBJECT

private slots:
    // --- Frontmatter -------------------------------------------------------

    void frontmatterPropertiesAndTypes()
    {
        const ParsedNote note = parse("---\n"
                                      "status: Active\n"
                                      "count: 42\n"
                                      "ratio: 0.5\n"
                                      "decided: 2026-10-12\n"
                                      "quoted: \"2026-10-12\"\n"
                                      "tags: [alpha, project/coeus]\n"
                                      "aliases:\n  - Coeus\n  - The Pole\n"
                                      "repo:\n"
                                      "---\n"
                                      "# Body\n");
        QVERIFY(!note.frontmatterError);
        QCOMPARE(note.bodyStartLine, 13);
        QCOMPARE(findProperty(note, QStringLiteral("status"))->type, PropertyType::Text);
        QCOMPARE(findProperty(note, QStringLiteral("status"))->values, QStringList{QStringLiteral("Active")});
        QCOMPARE(findProperty(note, QStringLiteral("count"))->type, PropertyType::Number);
        QCOMPARE(findProperty(note, QStringLiteral("ratio"))->type, PropertyType::Number);
        QCOMPARE(findProperty(note, QStringLiteral("decided"))->type, PropertyType::Date);
        QCOMPARE(findProperty(note, QStringLiteral("quoted"))->type, PropertyType::Text);
        QCOMPARE(findProperty(note, QStringLiteral("tags"))->type, PropertyType::List);
        QCOMPARE(findProperty(note, QStringLiteral("repo"))->values, QStringList{QString()});
        QCOMPARE(findProperty(note, QStringLiteral("decided"))->line, 5);
        QCOMPARE(note.aliases, (QStringList{QStringLiteral("Coeus"), QStringLiteral("The Pole")}));
        QCOMPARE(tagNames(note), (QStringList{QStringLiteral("alpha"), QStringLiteral("project/coeus")}));
        QCOMPARE(note.tags.front().line, 7);
        QCOMPARE(note.headings.front().line, 13);
    }

    void frontmatterTagsAsString()
    {
        const ParsedNote note = parse("---\ntags: \"#one, two three\"\n---\n");
        QCOMPARE(tagNames(note), (QStringList{QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")}));
    }

    void invalidFrontmatterIsReportedNotFatal()
    {
        const ParsedNote note = parse("---\ntitle: ok\nbroken: [unclosed\n---\nBody with #tag\n");
        QVERIFY(note.frontmatterError.has_value());
        QVERIFY(note.frontmatterError->line >= 2);
        QVERIFY(note.properties.isEmpty());
        QCOMPARE(tagNames(note), QStringList{QStringLiteral("tag")});
    }

    void nonMapFrontmatterIsAnError()
    {
        QVERIFY(parse("---\n- a\n- b\n---\n").frontmatterError.has_value());
    }

    void unclosedFrontmatterIsBody()
    {
        const ParsedNote note = parse("---\ntitle: x\n\nText #here\n");
        QVERIFY(note.properties.isEmpty());
        QCOMPARE(note.bodyStartLine, 1);
        QCOMPARE(tagNames(note), QStringList{QStringLiteral("here")});
    }

    void frontmatterOnlyAtByteZero()
    {
        const ParsedNote note = parse("\n---\ntitle: x\n---\n");
        QVERIFY(note.properties.isEmpty());
    }

    void frontmatterWithBomCrlfAndDotsTerminator()
    {
        const ParsedNote note = parse("\xEF\xBB\xBF---\r\nstatus: Active\r\n...\r\n# H\r\n");
        QVERIFY(!note.frontmatterError);
        QCOMPARE(findProperty(note, QStringLiteral("status"))->values, QStringList{QStringLiteral("Active")});
        QCOMPARE(note.headings.front().line, 4);
    }

    void emptyFrontmatter()
    {
        const ParsedNote note = parse("---\n---\nBody\n");
        QVERIFY(!note.frontmatterError);
        QVERIFY(note.properties.isEmpty());
        QCOMPARE(note.plainText, QStringLiteral("Body"));
    }

    // --- Wikilinks ---------------------------------------------------------

    void wikilinkForms()
    {
        const ParsedNote note = parse("See [[Alpha]], [[Beta|the beta]], [[Gamma#Design notes]],\n"
                                      "[[Delta#^abc-1]] and [[Epsilon^def]] and ![[diagram.png]].\n");
        QCOMPARE(note.links.size(), 6);

        QCOMPARE(note.links[0].target, QStringLiteral("Alpha"));
        QCOMPARE(note.links[0].style, LinkStyle::Wiki);
        QVERIFY(note.links[0].alias.isEmpty());
        QVERIFY(!note.links[0].embed);

        QCOMPARE(note.links[1].target, QStringLiteral("Beta"));
        QCOMPARE(note.links[1].alias, QStringLiteral("the beta"));

        QCOMPARE(note.links[2].target, QStringLiteral("Gamma"));
        QCOMPARE(note.links[2].heading, QStringLiteral("Design notes"));

        QCOMPARE(note.links[3].target, QStringLiteral("Delta"));
        QCOMPARE(note.links[3].block, QStringLiteral("abc-1"));
        QCOMPARE(note.links[3].line, 2);

        QCOMPARE(note.links[4].target, QStringLiteral("Epsilon"));
        QCOMPARE(note.links[4].block, QStringLiteral("def"));

        QCOMPARE(note.links[5].target, QStringLiteral("diagram.png"));
        QVERIFY(note.links[5].embed);
    }

    void embedsWithHeadingAndAlias()
    {
        const ParsedNote note = parse("Intro\n\n![[Note#Section|shown]] text #real\n");
        QCOMPARE(note.links.size(), 1);
        QCOMPARE(note.links[0].target, QStringLiteral("Note"));
        QCOMPARE(note.links[0].heading, QStringLiteral("Section"));
        QCOMPARE(note.links[0].alias, QStringLiteral("shown"));
        QCOMPARE(note.links[0].line, 3);
        QVERIFY(note.links[0].embed);
        QCOMPARE(tagNames(note), QStringList{QStringLiteral("real")});
    }

    void wikilinkPathAndCrossRoot()
    {
        const ParsedNote note = parse("[[projects/coeus/DECISIONS]] [[pontus:DECISIONS]]");
        QCOMPARE(note.links[0].target, QStringLiteral("projects/coeus/DECISIONS"));
        QCOMPARE(note.links[1].target, QStringLiteral("pontus:DECISIONS"));
    }

    void linksInCodeAreIgnored()
    {
        const ParsedNote note = parse("`[[NotALink]]`\n\n```\n[[AlsoNot]] #nottag\n```\n\n    [[Indented]]\n");
        QVERIFY(note.links.isEmpty());
        QVERIFY(note.tags.isEmpty());
    }

    void linkLineNumbersCountFrontmatter()
    {
        const ParsedNote note = parse("---\na: 1\n---\n\nPara one.\n\nPara &amp; [[Target]]\n");
        QCOMPARE(note.links.front().line, 7);
    }

    // --- Markdown links ----------------------------------------------------

    void markdownLinksToLocalFiles()
    {
        const ParsedNote note = parse("[Features](FEATURES.md), [spec](SPEC.md#Index%20schema),\n"
                                      "![mock](docs/design/main-window.png), [web](https://example.org),\n"
                                      "[mail](mailto:a@b.c), [anchor](#local), <https://auto.link>,\n"
                                      "[spaced](my%20note.md)\n");
        QCOMPARE(note.links.size(), 4);
        QCOMPARE(note.links[0].style, LinkStyle::Markdown);
        QCOMPARE(note.links[0].target, QStringLiteral("FEATURES.md"));
        QCOMPARE(note.links[0].alias, QStringLiteral("Features"));
        QCOMPARE(note.links[1].target, QStringLiteral("SPEC.md"));
        QCOMPARE(note.links[1].heading, QStringLiteral("Index schema"));
        QCOMPARE(note.links[2].target, QStringLiteral("docs/design/main-window.png"));
        QVERIFY(note.links[2].embed);
        QCOMPARE(note.links[2].line, 2);
        QCOMPARE(note.links[3].target, QStringLiteral("my note.md"));
    }

    // --- Tags --------------------------------------------------------------

    void inlineTags()
    {
        const ParsedNote note = parse("#start of paragraph, mid #middle, nested #a/b/c, kebab #to-do,\n"
                                      "issue #42 is not a tag, but #42nd is; email@x#nope; ##double;\n"
                                      "trailing slash #dir/ and `#code` and [#inlink](x.md).\n");
        QCOMPARE(tagNames(note), (QStringList{QStringLiteral("start"), QStringLiteral("middle"),
                                              QStringLiteral("a/b/c"), QStringLiteral("to-do"),
                                              QStringLiteral("42nd"), QStringLiteral("dir")}));
        QCOMPARE(note.tags[4].line, 2);
    }

    void tagsInHeadingsAndLists()
    {
        const ParsedNote note = parse("# Ideas #draft\n\n- item #listed\n- [ ] task #todo\n");
        QCOMPARE(tagNames(note), (QStringList{QStringLiteral("draft"), QStringLiteral("listed"),
                                              QStringLiteral("todo")}));
        QCOMPARE(note.tags[2].line, 4);
    }

    void unicodeTags()
    {
        QCOMPARE(tagNames(parse("Greek #Κοῖος here")), QStringList{QStringLiteral("Κοῖος")});
    }

    void urlFragmentsAreNotTags()
    {
        QVERIFY(parse("See https://example.org/page#section for more.").tags.isEmpty());
    }

    // --- Headings ----------------------------------------------------------

    void headings()
    {
        const ParsedNote note = parse("# Title\n\nText\n\n## Section with [[Link|label]] and `code`\n\n"
                                      "Setext\n======\n\n#\n\n#### Deep\n");
        QCOMPARE(note.headings.size(), 4);
        QCOMPARE(note.headings[0].level, 1);
        QCOMPARE(note.headings[0].text, QStringLiteral("Title"));
        QCOMPARE(note.headings[0].line, 1);
        QCOMPARE(note.headings[1].level, 2);
        QCOMPARE(note.headings[1].text, QStringLiteral("Section with label and code"));
        QCOMPARE(note.headings[1].line, 5);
        QCOMPARE(note.headings[2].text, QStringLiteral("Setext"));
        QCOMPARE(note.headings[2].line, 7);
        QCOMPARE(note.headings[3].level, 4);
        QCOMPARE(note.headings[3].line, 12);
    }

    // --- Block IDs ---------------------------------------------------------

    void blockIds()
    {
        const ParsedNote note = parse("A paragraph that can be cited. ^cite-me\n\n"
                                      "Two lines\nending here ^second\n\n"
                                      "- list item ^item1\n\n"
                                      "Caret^inside is not an ID.\n\n"
                                      "```\ncode ^notid\n```\n");
        QCOMPARE(note.blocks.size(), 3);
        QCOMPARE(note.blocks[0].id, QStringLiteral("cite-me"));
        QCOMPARE(note.blocks[0].line, 1);
        QCOMPARE(note.blocks[1].id, QStringLiteral("second"));
        QCOMPARE(note.blocks[1].line, 4);
        QCOMPARE(note.blocks[2].id, QStringLiteral("item1"));
        QCOMPARE(note.blocks[2].line, 6);
    }

    // --- Plain text and purity ---------------------------------------------

    void plainTextExcludesFrontmatterAndMarkup()
    {
        const ParsedNote note = parse("---\nsecret: hidden\n---\n# Heading\n\nSome **bold** [[Link|label]].\n");
        QVERIFY(!note.plainText.contains(QStringLiteral("secret")));
        QVERIFY(note.plainText.contains(QStringLiteral("Heading")));
        QVERIFY(note.plainText.contains(QStringLiteral("Some bold label.")));
        QVERIFY(!note.plainText.contains(QLatin1Char('*')));
    }

    void emptyNote()
    {
        const ParsedNote note = parse("");
        QVERIFY(note.links.isEmpty());
        QVERIFY(note.headings.isEmpty());
        QVERIFY(note.plainText.isEmpty());
    }

    void sameInputSameOutput()
    {
        const char* text = "---\ntags: [x]\n---\n# T\n[[A]] #b ^c\n";
        const ParsedNote a = parse(text);
        const ParsedNote b = parse(text);
        QCOMPARE(a.plainText, b.plainText);
        QCOMPARE(tagNames(a), tagNames(b));
        QCOMPARE(a.links.size(), b.links.size());
        QCOMPARE(a.blocks.size(), b.blocks.size());
    }
};

QTEST_GUILESS_MAIN(TestNoteParser)
#include "test_note_parser.moc"
