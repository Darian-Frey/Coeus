// F-025: fenced code coloured by language in reading mode, and Transclusion's
// heading sections for embeds (F-023).

#include "parser/CodeHighlighter.hpp"
#include "parser/Transclusion.hpp"

#include <QTest>

using namespace coeus;

namespace {

QString kw(const char* text) { return QStringLiteral("<span class=\"kw\">%1</span>").arg(QLatin1String(text)); }
QString ty(const char* text) { return QStringLiteral("<span class=\"ty\">%1</span>").arg(QLatin1String(text)); }
QString st(const QString& text) { return QStringLiteral("<span class=\"st\">%1</span>").arg(text); }
QString cm(const QString& text) { return QStringLiteral("<span class=\"cm\">%1</span>").arg(text); }
QString nu(const char* text) { return QStringLiteral("<span class=\"nu\">%1</span>").arg(QLatin1String(text)); }

} // namespace

class TestCodeHighlighter : public QObject {
    Q_OBJECT

private slots:
    void cFamily()
    {
        const QString html = CodeHighlighter::toHtml(
            QStringLiteral("#include <vector>\nint main() { return 42; } // done\n/* a\n b */ auto s = \"x // y\";"), QStringLiteral("C++"));
        QVERIFY(html.startsWith(kw("#include") + QStringLiteral(" &lt;") + ty("vector") + QStringLiteral("&gt;\n")));
        QVERIFY(html.contains(ty("int") + QStringLiteral(" main() { ") + kw("return") + QLatin1Char(' ') + nu("42")));
        QVERIFY(html.contains(cm(QStringLiteral("// done"))));
        QVERIFY(html.contains(cm(QStringLiteral("/* a\n b */"))));
        QVERIFY(html.contains(st(QStringLiteral("&quot;x // y&quot;")))); // no comment inside a string
        QVERIFY(!html.contains(nu("2"))); // within 42 only once
    }

    void pythonShellAndSql()
    {
        QCOMPARE(CodeHighlighter::toHtml(QStringLiteral("s = '''a\n# not a comment'''  # yes"), QStringLiteral("py")),
                 QStringLiteral("s = ") + st(QStringLiteral("'''a\n# not a comment'''")) + QStringLiteral("  ") + cm(QStringLiteral("# yes")));
        const QString shell = CodeHighlighter::toHtml(QStringLiteral("if [ -n \"$HOME\" ]; then echo ${#ARR} $USER; fi # end"),
                                                      QStringLiteral("bash"));
        QVERIFY(shell.startsWith(kw("if")));
        QVERIFY(shell.contains(ty("${#ARR}"))); // "#" inside ${…} is not a comment
        QVERIFY(shell.contains(ty("$USER")));
        QVERIFY(shell.endsWith(cm(QStringLiteral("# end"))));
        const QString sql = CodeHighlighter::toHtml(QStringLiteral("SELECT count(*) FROM notes -- all"), QStringLiteral("SQL"));
        QVERIFY(sql.startsWith(kw("SELECT") + QLatin1Char(' ') + ty("count")));
        QVERIFY(sql.contains(kw("FROM")));
        QVERIFY(sql.endsWith(cm(QStringLiteral("-- all"))));
    }

    void configFormats()
    {
        const QString yaml = CodeHighlighter::toHtml(QStringLiteral("tags: [a, b]\n- name: x # why\nurl: \"http://a#b\""), QStringLiteral("yaml"));
        QVERIFY(yaml.startsWith(ty("tags") + QLatin1Char(':')));
        QVERIFY(yaml.contains(QStringLiteral("- ") + ty("name") + QLatin1Char(':')));
        QVERIFY(yaml.contains(cm(QStringLiteral("# why"))));
        QVERIFY(yaml.contains(st(QStringLiteral("&quot;http://a#b&quot;"))));
        const QString toml = CodeHighlighter::toHtml(QStringLiteral("[mcp]\nallow_writes = true"), QStringLiteral("toml"));
        QCOMPARE(toml, kw("[mcp]") + QLatin1Char('\n') + ty("allow_writes") + QStringLiteral(" = ") + kw("true"));
        QCOMPARE(CodeHighlighter::toHtml(QStringLiteral("{\"a\": 1, \"b\": null}"), QStringLiteral("json")),
                 QStringLiteral("{") + st(QStringLiteral("&quot;a&quot;")) + QStringLiteral(": ") + nu("1") + QStringLiteral(", ")
                     + st(QStringLiteral("&quot;b&quot;")) + QStringLiteral(": ") + kw("null") + QStringLiteral("}"));
        const QString diff = CodeHighlighter::toHtml(QStringLiteral("@@ -1 +1 @@\n-old\n+new\n same"), QStringLiteral("diff"));
        QCOMPARE(diff, kw("@@ -1 +1 @@") + QLatin1Char('\n') + cm(QStringLiteral("-old")) + QLatin1Char('\n')
                           + st(QStringLiteral("+new")) + QStringLiteral("\n same"));
    }

    void unknownLanguagesAreOnlyEscaped()
    {
        QCOMPARE(CodeHighlighter::toHtml(QStringLiteral("if <x> & \"y\""), QStringLiteral("brainfuck")),
                 QStringLiteral("if &lt;x&gt; &amp; &quot;y&quot;"));
        QVERIFY(!CodeHighlighter::knows(QStringLiteral("brainfuck")));
        QVERIFY(CodeHighlighter::knows(QStringLiteral(" Rust ")));
        QVERIFY(CodeHighlighter::languages().size() >= 30);
    }

    void headingSections()
    {
        const QByteArray note = "---\ntitle: x\n---\n# Top\n\nIntro\n\n## Install\n\nStep\n\n```\n## not a heading\n```\n\n### Deeper\n\nkept\n\n## Use\n\nnot kept\n";
        QCOMPARE(Transclusion::body(note).left(5), QByteArray("# Top"));
        const auto install = Transclusion::section(note, QStringLiteral("  install "));
        QVERIFY(install);
        QVERIFY(install->startsWith("## Install\n"));
        QVERIFY(install->contains("## not a heading")); // inside the fence: not a boundary
        QVERIFY(install->contains("### Deeper\n\nkept"));
        QVERIFY(!install->contains("## Use"));
        QVERIFY(Transclusion::section(note, QStringLiteral("Top"))->contains("not kept")); // level 1 runs to the end
        QVERIFY(!Transclusion::section(note, QStringLiteral("Missing")));
    }
};

QTEST_GUILESS_MAIN(TestCodeHighlighter)
#include "test_code_highlighter.moc"
