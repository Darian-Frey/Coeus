#include "IndexDump.hpp"

#include "index/Backlinks.hpp"
#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

QStringList where(const QList<Backlinks::Reference>& refs)
{
    QStringList out;
    for (const auto& r : refs)
        out.append(QStringLiteral("%1:%2").arg(r.sourceDisplay).arg(r.line));
    return out;
}

} // namespace

class TestBacklinks : public QObject {
    Q_OBJECT

private slots:
    void findsWholeWordMentionsOutsideLinksAndCode()
    {
        const QString text = QStringLiteral("---\ntitle: Alpha in frontmatter\n---\n"
                                            "We chose alpha here.\n"           // 4: mention, any case
                                            "See [[Alpha]] already linked.\n"  // 5: linked, not a mention
                                            "Alphabet soup.\n"                 // 6: not a whole word
                                            "`Alpha` in code.\n"               // 7: code span
                                            "```\nAlpha in a fence\n```\n"     // 8-10: fenced
                                            "[the Alpha doc](a.md) link text\n" // 11: Markdown link
                                            "The Pole and Alpha_x and (Alpha).\n"); // 12: alias, and (Alpha)
        const auto hits = Backlinks::findMentions(text, {QStringLiteral("Alpha"), QStringLiteral("The Pole")});
        QList<int> lines;
        for (const auto& h : hits)
            lines.append(h.line);
        QCOMPARE(lines, (QList<int>{4, 12}));
        QCOMPARE(hits[0].lineText.mid(hits[0].column, hits[0].length), QStringLiteral("alpha"));
    }

    void shortTermsAreIgnored()
    {
        QVERIFY(Backlinks::findMentions(QStringLiteral("an AI wrote it"), {QStringLiteral("AI")}).isEmpty());
    }

    void contextKeepsTheMatchVisible()
    {
        const QString line = QString(300, QLatin1Char('x')) + QStringLiteral(" target ") + QString(300, QLatin1Char('y'));
        const QString cut = Backlinks::context(line, 301, 6);
        QVERIFY(cut.contains(QStringLiteral("target")));
        QVERIFY(cut.startsWith(QStringLiteral("…")) && cut.endsWith(QStringLiteral("…")));
        QVERIFY(cut.size() <= Backlinks::kContextChars + 2);
        QCOMPARE(Backlinks::context(QStringLiteral("   short line  "), 3, 5), QStringLiteral("short line"));
    }

    void collectsLinksAndMentionsAcrossRoots()
    {
        QTemporaryDir vault, repo;
        writeFile(vault.filePath(QStringLiteral("Alpha.md")), "---\naliases: [First Letter]\n---\n# Alpha\nSelf [[Alpha]]\n");
        writeFile(vault.filePath(QStringLiteral("b.md")), "# B\n\nLinks to [[Alpha]] here.\nAnd ![[Alpha]] embedded.\n");
        writeFile(vault.filePath(QStringLiteral("notes/c.md")), "Mentions alpha without linking.\nAnd the first letter too.\n");
        writeFile(vault.filePath(QStringLiteral("d.md")), "Nothing relevant.\n");
        writeFile(repo.filePath(QStringLiteral("README.md")), "Cross-root [[vault:Alpha]].\n");

        const QString index = vault.filePath(QStringLiteral("index.db"));
        {
            auto store = IndexStore::open(index);
            Indexer indexer(*store);
            QVERIFY(indexer.scan({0, QStringLiteral("vault"), vault.path()}).has_value());
            QVERIFY(indexer.scan({1, QStringLiteral("repo"), repo.path()}).has_value());
        }

        const auto result = Backlinks::collect(index, vault.filePath(QStringLiteral("Alpha.md")));
        QVERIFY2(result.has_value(), qPrintable(result.error()));
        QCOMPARE(where(result->links), (QStringList{QStringLiteral("b.md:3"), QStringLiteral("b.md:4"),
                                                   QStringLiteral("repo/README.md:1")}));
        QCOMPARE(result->links[0].context, QStringLiteral("Links to [[Alpha]] here."));
        QVERIFY(!result->links[0].embed);
        QVERIFY(result->links[1].embed);
        QCOMPARE(where(result->mentions), (QStringList{QStringLiteral("notes/c.md:1"), QStringLiteral("notes/c.md:2")}));

        // A note nothing links to, and one not in the index.
        QVERIFY(Backlinks::collect(index, vault.filePath(QStringLiteral("d.md")))->links.isEmpty());
        QVERIFY(Backlinks::collect(index, vault.filePath(QStringLiteral("new.md")))->links.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBacklinks)
#include "test_backlinks.moc"
