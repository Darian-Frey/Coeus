// F-026: the link graph read from the index, its options, and the local
// graph as a neighbourhood of it.

#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"
#include "index/LinkGraph.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-14\n"
                           "> **Why this status:** T\n\n# Alpha\n\nSee [[a]].\n";

QStringList keys(const LinkGraph& graph)
{
    QStringList list;
    for (const LinkGraph::Node& node : graph.nodes())
        list.append(node.key);
    list.sort();
    return list;
}

} // namespace

class TestLinkGraph : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_vault;
    QTemporaryDir m_repo;
    QString m_index;

    QString note(const char* path) const { return m_vault.filePath(QLatin1String(path)); }

private slots:
    void initTestCase()
    {
        writeFile(note("a.md"), "# A\n\n[[b]] and [[Future idea]] and ![[picture.png]].\n\n#area/x\n");
        writeFile(note("b.md"), "# B\n\n[[c]], [[a]] again.\n");
        writeFile(note("c.md"), "# C\n\n#area\n");
        writeFile(note("d.md"), "# D\n\nAlone.\n");
        writeFile(note("projects/alpha/README.md"), kHeader);
        writeFile(m_repo.filePath(QStringLiteral("README.md")), "# Repo\n\nCross-root [[vault:a]].\n");
        m_index = m_vault.filePath(QStringLiteral(".coeus/index.db"));
        QDir().mkpath(m_vault.filePath(QStringLiteral(".coeus")));
        auto store = IndexStore::open(m_index);
        QVERIFY(store.has_value());
        Indexer indexer(*store);
        QVERIFY(indexer.scan({0, QStringLiteral("vault"), m_vault.path()}).has_value());
        QVERIFY(indexer.scan({1, QStringLiteral("repo"), m_repo.path()}).has_value());
    }

    void notesLinksAndTagsByDefault()
    {
        const auto graph = LinkGraph::load(m_index, {});
        QVERIFY2(graph.has_value(), qPrintable(graph.error()));
        // No orphan d, no ghost, nothing from the linked root.
        QCOMPARE(keys(*graph), (QStringList{QStringLiteral("n:vault/a.md"), QStringLiteral("n:vault/b.md"), QStringLiteral("n:vault/c.md"),
                                            QStringLiteral("n:vault/projects/alpha/README.md"), QStringLiteral("t:area"),
                                            QStringLiteral("t:area/x")}));
        const LinkGraph::Node& a = graph->nodes()[graph->indexOf(QStringLiteral("n:vault/a.md"))];
        QCOMPARE(a.path, note("a.md"));
        QCOMPARE(a.label, QStringLiteral("a")); // the file name, without a title
        QCOMPARE(a.tag, QStringLiteral("area")); // the top of #area/x
        QCOMPARE(a.degree, 3);                   // b (both ways, once), README, #area/x
        QCOMPARE(graph->nodes()[graph->indexOf(QStringLiteral("n:vault/projects/alpha/README.md"))].project, QStringLiteral("alpha"));
        QCOMPARE(graph->nodes()[graph->indexOf(QStringLiteral("t:area"))].label, QStringLiteral("#area"));
        QCOMPARE(graph->edges().size(), 5); // a–b, b–c, README–a, a–#area/x, c–#area
        QCOMPARE(graph->noteCount(), 4);
    }

    void optionsAddAndRemoveNodes()
    {
        LinkGraph::Options options;
        options.tags = false;
        auto graph = LinkGraph::load(m_index, options);
        QCOMPARE(graph->indexOf(QStringLiteral("t:area")), -1);
        QCOMPARE(graph->nodes()[graph->indexOf(QStringLiteral("n:vault/c.md"))].tag, QStringLiteral("area")); // still coloured

        options = {};
        options.orphans = true;
        QVERIFY(LinkGraph::load(m_index, options)->indexOf(QStringLiteral("n:vault/d.md")) >= 0);

        options = {};
        options.unresolved = true;
        graph = LinkGraph::load(m_index, options);
        const int ghost = graph->indexOf(QStringLiteral("g:future idea"));
        QVERIFY(ghost >= 0);
        QCOMPARE(graph->nodes()[ghost].kind, LinkGraph::Node::Kind::Ghost);
        QCOMPARE(graph->nodes()[ghost].label, QStringLiteral("Future idea"));
        QCOMPARE(graph->indexOf(QStringLiteral("g:picture.png")), -1); // a missing attachment is no note

        options = {};
        options.linkedRoots = true;
        graph = LinkGraph::load(m_index, options);
        const int repo = graph->indexOf(QStringLiteral("n:repo/README.md"));
        QVERIFY(repo >= 0);
        QCOMPARE(graph->nodes()[repo].path, m_repo.filePath(QStringLiteral("README.md")));
    }

    void neighbourhoodsByDepth()
    {
        const auto graph = LinkGraph::load(m_index, {});
        const LinkGraph near = graph->neighbourhood(note("a.md"), 1);
        QCOMPARE(near.nodes().front().key, QStringLiteral("n:vault/a.md")); // the centre first
        QCOMPARE(keys(near), (QStringList{QStringLiteral("n:vault/a.md"), QStringLiteral("n:vault/b.md"),
                                          QStringLiteral("n:vault/projects/alpha/README.md"), QStringLiteral("t:area/x")}));
        QCOMPARE(near.edges().size(), 3);
        const LinkGraph further = graph->neighbourhood(note("a.md"), 2);
        QVERIFY(further.indexOf(QStringLiteral("n:vault/c.md")) >= 0);
        QCOMPARE(further.indexOf(QStringLiteral("t:area")), -1); // three steps away
        QVERIFY(graph->neighbourhood(note("d.md"), 2).nodes().isEmpty()); // not in this graph
    }

    void edgesAreUndirectedAndUnique()
    {
        LinkGraph graph;
        const int a = graph.addNode({LinkGraph::Node::Kind::Note, QStringLiteral("n:a"), {}, {}, {}, {}, 0});
        const int b = graph.addNode({LinkGraph::Node::Kind::Note, QStringLiteral("n:b"), {}, {}, {}, {}, 0});
        QCOMPARE(graph.addNode({LinkGraph::Node::Kind::Note, QStringLiteral("n:a"), {}, {}, {}, {}, 0}), a);
        graph.addEdge(a, b);
        graph.addEdge(b, a);
        graph.addEdge(a, a);
        QCOMPARE(graph.edges().size(), 1);
        QCOMPARE(graph.nodes()[a].degree, 1);
    }
};

QTEST_GUILESS_MAIN(TestLinkGraph)
#include "test_link_graph.moc"
