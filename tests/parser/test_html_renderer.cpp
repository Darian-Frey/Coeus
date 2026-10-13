#include "parser/HtmlRenderer.hpp"

#include <QTest>

using coeus::HtmlRenderer;

namespace {

QString render(const char* markdown)
{
    HtmlRenderer::Options options;
    options.resolveImage = [](const QString& target, bool wiki) {
        if (target.startsWith(QLatin1String("missing")))
            return QString();
        return QStringLiteral("file:///v/%1%2").arg(wiki ? QStringLiteral("w/") : QString(), target);
    };
    return HtmlRenderer::render(QByteArrayView(markdown), options);
}

} // namespace

class TestHtmlRenderer : public QObject {
    Q_OBJECT

private slots:
    void blocks()
    {
        const QString html = render("# Title\n\nPara **bold** *em* ~~del~~\n\n- a\n- b\n\n3. three\n\n---\n");
        QVERIFY(html.contains(QStringLiteral("<h1>Title</h1>")));
        QVERIFY(html.contains(QStringLiteral("<b>bold</b> <i>em</i> <s>del</s>")));
        QVERIFY(html.contains(QStringLiteral("<ul><li>a</li><li>b</li></ul>")));
        QVERIFY(html.contains(QStringLiteral("<ol start=\"3\"><li>three</li></ol>")));
        QVERIFY(html.contains(QStringLiteral("<hr/>")));
    }

    void codeIsEscapedAndNeverDecorated()
    {
        const QString html = render("`<b>#tag [[x]]</b>`\n\n```cpp\nif (a < b) { ==no== } // #nottag ![[img.png]]\n```\n");
        QVERIFY(html.contains(QStringLiteral("<code>&lt;b&gt;#tag [[x]]&lt;/b&gt;</code>")));
        QVERIFY(html.contains(QStringLiteral("<pre class=\"lang-cpp\"><code>if (a &lt; b) { ==no== } // #nottag ![[img.png]]")));
        QVERIFY(!html.contains(QStringLiteral("coeus-tag")));
        QVERIFY(!html.contains(QStringLiteral("<img")));
    }

    void rawHtmlIsShownNotRun()
    {
        const QString html = render("<script>alert(1)</script>\n\nText <b>inline</b>\n");
        QVERIFY(!html.contains(QStringLiteral("<script>")));
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
        QVERIFY(html.contains(QStringLiteral("&lt;b&gt;inline&lt;/b&gt;")));
    }

    void links()
    {
        const QString html = render("[[Alpha]] [[Beta|the beta]] [[Gamma#Design notes]] [[two words]]\n"
                                    "[Spec](SPEC.md#Index) [web](https://example.org) [top](#here)\n");
        QVERIFY(html.contains(QStringLiteral("<a href=\"coeus-wiki:Alpha\">Alpha</a>")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"coeus-wiki:Beta\">the beta</a>")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"coeus-wiki:Gamma#Design%20notes\">")));
        QVERIFY(html.contains(QStringLiteral("coeus-wiki:two%20words")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"coeus-file:SPEC.md#Index\">Spec</a>")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"https://example.org\">web</a>")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"#here\">top</a>")));
    }

    void tagsHighlightsAndEmbeds()
    {
        const QString html = render("A #tag, #a/b, not #42, ==bright== and ![[pic.png|Diagram]] and ![[Note#Part]] "
                                    "and ![[missing.png]] and ![alt](img/x.png)\n");
        QVERIFY(html.contains(QStringLiteral("<a class=\"tag\" href=\"coeus-tag:tag\">#tag</a>")));
        QVERIFY(html.contains(QStringLiteral("href=\"coeus-tag:a/b\">#a/b</a>")));
        QVERIFY(html.contains(QStringLiteral("not #42,")));
        QVERIFY(html.contains(QStringLiteral("<span class=\"highlight\">bright</span>")));
        QVERIFY(html.contains(QStringLiteral("<img src=\"file:///v/w/pic.png\" alt=\"Diagram\"/>")));
        QVERIFY(html.contains(QStringLiteral("<a class=\"embed\" href=\"coeus-wiki:Note#Part\">↪ Note#Part</a>")));
        QVERIFY(html.contains(QStringLiteral("<span class=\"missing\">[missing.png]</span>")));
        QVERIFY(html.contains(QStringLiteral("<img src=\"file:///v/img/x.png\" alt=\"alt\"/>")));
    }

    void calloutsAndQuotes()
    {
        const QString html = render("> [!warning] Mind the gap\n> Body text.\n\n> [!tip]\n> Short.\n\n> Plain quote.\n");
        QVERIFY(html.contains(QStringLiteral("<b>Mind the gap</b>")));
        QVERIFY(html.contains(QStringLiteral("Body text.")));
        QVERIFY(html.contains(QStringLiteral("<b>Tip</b>")));
        QCOMPARE(html.count(QStringLiteral("class=\"callout\"")), 2);
        QVERIFY(html.contains(QStringLiteral("<blockquote><p>Plain quote.</p></blockquote>")));
    }

    void tasksTablesBlockIdsAndBreaks()
    {
        const QString html = render("- [ ] todo\n- [x] done ^blk\n\n| A | B |\n|:---|---:|\n| 1 | 2 |\n\n"
                                    "line one\nline two ^para\n");
        QVERIFY(html.contains(QStringLiteral("<ul style=\"list-style-type: none;\"><li>☐ todo</li>")));
        QVERIFY(render("- [ ] task\n- plain\n").startsWith(QStringLiteral("<ul><li>"))); // mixed keeps bullets
        QVERIFY(html.contains(QStringLiteral("<li>☑ done</li>"))); // block ID hidden
        QVERIFY(html.contains(QStringLiteral("<th>A</th>"))); // left is the default
        QVERIFY(html.contains(QStringLiteral("<td align=\"right\">2</td>")));
        QVERIFY(html.contains(QStringLiteral("<p>line one<br/>line two</p>")));
        QVERIFY(!html.contains(QStringLiteral("^para")));
    }

    void frontmatterBecomesProperties()
    {
        const QString html = render("---\nstatus: Accepted\ntags: [a, b]\n---\n# Body\n");
        QVERIFY(html.startsWith(QStringLiteral("<table class=\"properties\"")));
        QVERIFY(html.contains(QStringLiteral("<td class=\"key\">status</td><td>Accepted</td>")));
        QVERIFY(html.contains(QStringLiteral("<td class=\"key\">tags</td><td>a, b</td>")));
        QVERIFY(!html.contains(QStringLiteral("---")));

        const QString broken = render("---\nbad: [\n---\nBody\n");
        QVERIFY(broken.contains(QStringLiteral("class=\"problem\"")));
        QVERIFY(broken.contains(QStringLiteral("Body")));
    }

    void mathsShowsAsCode()
    {
        QVERIFY(render("Energy $E = mc^2$.\n").contains(QStringLiteral("<code class=\"math\">E = mc^2</code>")));
    }
};

QTEST_GUILESS_MAIN(TestHtmlRenderer)
#include "test_html_renderer.moc"
