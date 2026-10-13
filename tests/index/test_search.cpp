#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"
#include "index/Search.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

class TestSearch : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_vault;
    QTemporaryDir m_repo;
    QString m_index;

    QStringList find(const QString& query)
    {
        const auto hits = Search::run(m_index, SearchQuery::parse(query));
        if (!hits)
            return {QStringLiteral("ERROR: ") + hits.error()};
        QStringList out;
        for (const auto& hit : *hits)
            out.append(hit.noteDisplay);
        return out;
    }

    static QStringList sorted(QStringList list)
    {
        list.sort();
        return list;
    }

private slots:
    // --- Parsing -----------------------------------------------------------

    void parsesEveryForm()
    {
        const SearchQuery q = SearchQuery::parse(
            QStringLiteral("vault  \"plain folder\" -draft -\"old idea\" tag:#Storage path:projects/ file:READ re:/D-0\\d+ x/"));
        QCOMPARE(q.terms, QStringList{QStringLiteral("vault")});
        QCOMPARE(q.phrases, QStringList{QStringLiteral("plain folder")});
        QCOMPARE(q.excluded, (QStringList{QStringLiteral("draft"), QStringLiteral("old idea")}));
        QCOMPARE(q.tags, QStringList{QStringLiteral("Storage")});
        QCOMPARE(q.paths, QStringList{QStringLiteral("projects/")});
        QCOMPARE(q.files, QStringList{QStringLiteral("READ")});
        QCOMPARE(q.patterns, QStringList{QStringLiteral("D-0\\d+ x")}); // a regex may hold spaces
        QVERIFY(q.error.isEmpty());
        QCOMPARE(q.ftsExpression(), QStringLiteral("\"plain folder\" AND \"vault\"*"));
    }

    void parsingEdgeCases()
    {
        QVERIFY(SearchQuery::parse(QString()).isEmpty());
        QVERIFY(SearchQuery::parse(QStringLiteral("   - tag: \"\" ... ")).isEmpty()); // half-typed: nothing to run
        QCOMPARE(SearchQuery::parse(QStringLiteral("\"unclosed phrase")).phrases, QStringList{QStringLiteral("unclosed phrase")});
        QVERIFY(!SearchQuery::parse(QStringLiteral("re:/([/")).error.isEmpty());
        QVERIFY(!SearchQuery::parse(QStringLiteral("-tag:x")).error.isEmpty());
        QCOMPARE(SearchQuery::ftsString(QStringLiteral("say \"hi\"")), QStringLiteral("\"say \"\"hi\"\"\""));
    }

    // --- Running -----------------------------------------------------------

    void initTestCase()
    {
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/DECISIONS.md")),
                  "# Decisions\n\nD-005 Vault is a plain folder. The index is a cache. #storage\n");
        writeFile(m_vault.filePath(QStringLiteral("projects/coeus/README.md")),
                  "# Coeus\n\nA local-first knowledge base. The vault holds notes. #storage/index\n");
        writeFile(m_vault.filePath(QStringLiteral("ideas/draft.md")), "# Draft\n\nVault idea, still a draft. #Idea\n");
        writeFile(m_vault.filePath(QStringLiteral("Vault notes.md")), "Nothing about storage here.\n");
        writeFile(m_repo.filePath(QStringLiteral("README.md")), "# Pontus\n\nPhysics, not a vault.\n");
        m_index = m_vault.filePath(QStringLiteral(".coeus-test-index.db"));
        auto store = IndexStore::open(m_index);
        Indexer indexer(*store);
        QVERIFY(indexer.scan({0, QStringLiteral("vault"), m_vault.path()}).has_value());
        QVERIFY(indexer.scan({1, QStringLiteral("pontus"), m_repo.path()}).has_value());
    }

    void wordsArePrefixesAndAnded()
    {
        QCOMPARE(sorted(find(QStringLiteral("vau"))).size(), 5); // prefix: every note says "vault"
        QCOMPARE(sorted(find(QStringLiteral("vault cache"))), QStringList{QStringLiteral("projects/coeus/DECISIONS.md")});
        QVERIFY(find(QStringLiteral("zzzz")).isEmpty());
    }

    void titleMatchesRankFirst()
    {
        // "Vault notes" has vault in its title only; the others in the body.
        QCOMPARE(find(QStringLiteral("vault")).front(), QStringLiteral("Vault notes.md"));
    }

    void phrasesAndExclusions()
    {
        QCOMPARE(find(QStringLiteral("\"plain folder\"")), QStringList{QStringLiteral("projects/coeus/DECISIONS.md")});
        QVERIFY(!find(QStringLiteral("vault -draft")).contains(QStringLiteral("ideas/draft.md")));
        QVERIFY(!find(QStringLiteral("vault -\"local-first\"")).contains(QStringLiteral("projects/coeus/README.md")));
    }

    void operators()
    {
        QCOMPARE(sorted(find(QStringLiteral("tag:storage"))),
                 (QStringList{QStringLiteral("projects/coeus/DECISIONS.md"), QStringLiteral("projects/coeus/README.md")}));
        QCOMPARE(find(QStringLiteral("tag:#idea")), QStringList{QStringLiteral("ideas/draft.md")}); // any case
        QCOMPARE(find(QStringLiteral("tag:stor")), QStringList()); // a tag, not a prefix of one
        QCOMPARE(sorted(find(QStringLiteral("path:projects/"))).size(), 2);
        QCOMPARE(find(QStringLiteral("path:pontus/")), QStringList{QStringLiteral("pontus/README.md")});
        QCOMPARE(sorted(find(QStringLiteral("file:readme"))),
                 (QStringList{QStringLiteral("pontus/README.md"), QStringLiteral("projects/coeus/README.md")}));
        QCOMPARE(find(QStringLiteral("re:/D-0\\d\\d/")), QStringList{QStringLiteral("projects/coeus/DECISIONS.md")});
        QCOMPARE(find(QStringLiteral("vault tag:storage file:read")), QStringList{QStringLiteral("projects/coeus/README.md")});
        QCOMPARE(find(QStringLiteral("path:100%_")), QStringList()); // LIKE wildcards are literal
    }

    void snippetsMarkMatches()
    {
        const auto hits = Search::run(m_index, SearchQuery::parse(QStringLiteral("cache")));
        QVERIFY(hits.has_value());
        QCOMPARE(hits->size(), 1);
        QVERIFY(hits->front().snippet.contains(QString(Search::kMarkStart) + QStringLiteral("cache") + Search::kMarkEnd));
    }

    void errorsAreReportedNotThrown()
    {
        QVERIFY(find(QStringLiteral("re:/([/")).front().startsWith(QStringLiteral("ERROR")));
        QVERIFY(!Search::run(m_vault.filePath(QStringLiteral("absent.db")), SearchQuery::parse(QStringLiteral("x"))).has_value());
        QVERIFY(Search::run(m_index, SearchQuery::parse(QString()))->isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSearch)
#include "test_search.moc"
