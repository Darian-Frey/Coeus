#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"
#include "index/Tags.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

Tags::Usage use(const QString& tag, const QString& note)
{
    return {tag, QStringLiteral("/v/") + note, note};
}

const Tags::Node* child(const QList<Tags::Node>& nodes, const QString& name)
{
    for (const Tags::Node& node : nodes) {
        if (node.name == name)
            return &node;
    }
    return nullptr;
}

} // namespace

class TestTags : public QObject {
    Q_OBJECT

private slots:
    void nestsAndCountsDistinctNotes()
    {
        const QList<Tags::Usage> usages{use(QStringLiteral("project/coeus"), QStringLiteral("a.md")),
                                        use(QStringLiteral("project/coeus"), QStringLiteral("a.md")), // twice in one note
                                        use(QStringLiteral("project/pontus"), QStringLiteral("b.md")),
                                        use(QStringLiteral("project"), QStringLiteral("c.md")),
                                        use(QStringLiteral("idea"), QStringLiteral("a.md"))};
        const QList<Tags::Node> tree = Tags::tree(usages);
        QCOMPARE(tree.size(), 2);
        QCOMPARE(tree[0].name, QStringLiteral("idea")); // sorted
        const Tags::Node* project = child(tree, QStringLiteral("project"));
        QVERIFY(project);
        QCOMPARE(project->noteCount, 3); // a, b and c, each once
        QCOMPARE(project->children.size(), 2);
        QCOMPARE(child(project->children, QStringLiteral("coeus"))->noteCount, 1);
        QCOMPARE(child(project->children, QStringLiteral("coeus"))->fullTag, QStringLiteral("project/coeus"));
    }

    void caseInsensitiveWithFirstSpelling()
    {
        const QList<Tags::Usage> usages{use(QStringLiteral("Index"), QStringLiteral("a.md")),
                                        use(QStringLiteral("index"), QStringLiteral("b.md")),
                                        use(QStringLiteral("INDEX/Sub"), QStringLiteral("c.md"))};
        const QList<Tags::Node> tree = Tags::tree(usages);
        QCOMPARE(tree.size(), 1);
        QCOMPARE(tree[0].name, QStringLiteral("Index"));
        QCOMPARE(tree[0].noteCount, 3);
        QCOMPARE(Tags::notesFor(usages, QStringLiteral("index")).size(), 3);
        QCOMPARE(Tags::notesFor(usages, QStringLiteral("index/sub")).size(), 1);
    }

    void notesForIncludesDescendantsOnly()
    {
        const QList<Tags::Usage> usages{use(QStringLiteral("a/b"), QStringLiteral("x.md")),
                                        use(QStringLiteral("ab"), QStringLiteral("y.md")),
                                        use(QStringLiteral("a"), QStringLiteral("z.md"))};
        QStringList names;
        for (const auto& u : Tags::notesFor(usages, QStringLiteral("a")))
            names.append(u.noteDisplay);
        QCOMPARE(names, (QStringList{QStringLiteral("x.md"), QStringLiteral("z.md")})); // not "ab"
    }

    void loadsInlineAndFrontmatterTagsAcrossRoots()
    {
        QTemporaryDir vault, repo;
        writeFile(vault.filePath(QStringLiteral("a.md")), "---\ntags: [storage]\n---\nInline #storage/index here.\n");
        writeFile(repo.filePath(QStringLiteral("README.md")), "#storage in a repo\n");
        const QString index = vault.filePath(QStringLiteral("index.db"));
        {
            auto store = IndexStore::open(index);
            Indexer indexer(*store);
            QVERIFY(indexer.scan({0, QStringLiteral("vault"), vault.path()}).has_value());
            QVERIFY(indexer.scan({1, QStringLiteral("repo"), repo.path()}).has_value());
        }
        const auto usages = Tags::load(index);
        QVERIFY2(usages.has_value(), qPrintable(usages.error()));
        QCOMPARE(usages->size(), 3);
        const QList<Tags::Node> tree = Tags::tree(*usages);
        QCOMPARE(tree.size(), 1);
        QCOMPARE(tree[0].noteCount, 2);
        QStringList where;
        for (const auto& u : Tags::notesFor(*usages, QStringLiteral("storage")))
            where.append(u.noteDisplay);
        QCOMPARE(where, (QStringList{QStringLiteral("a.md"), QStringLiteral("repo/README.md")}));
        QVERIFY(!Tags::load(vault.filePath(QStringLiteral("missing.db"))).has_value());
    }
};

QTEST_GUILESS_MAIN(TestTags)
#include "test_tags.moc"
