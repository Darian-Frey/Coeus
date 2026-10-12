#include "IndexDump.hpp"

#include "index/Indexer.hpp"
#include "index/IndexStore.hpp"
#include "index/LinkResolver.hpp"

#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace coeus;
using coeus::testing::writeFile;

class TestIndexer : public QObject {
    Q_OBJECT

private:
    // Target ("root:path") of the first link from `source` with `target_raw`.
    static QString targetOf(IndexStore& store, const QString& source, const QString& raw)
    {
        auto q = store.database().prepare(
            "SELECT t.root_id || ':' || t.path, l.ambiguous FROM links l "
            "JOIN notes s ON s.id = l.src LEFT JOIN notes t ON t.id = l.target_note "
            "WHERE s.root_id || ':' || s.path = ? AND l.target_raw = ? ORDER BY l.line LIMIT 1");
        q->bind(1, source).bind(2, raw);
        if (!q->step().value_or(false))
            return QStringLiteral("<no link>");
        if (q->int64(1))
            return QStringLiteral("<ambiguous>");
        return q->isNull(0) ? QStringLiteral("<unresolved>") : q->text(0);
    }

    static qint64 count(IndexStore& store, const char* sql)
    {
        auto q = store.database().prepare(sql);
        return q->step().value_or(false) ? q->int64(0) : -1;
    }

private slots:
    void scansNotesAndSkipsHiddenAndNonNotes()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n#tag [[b]]\n");
        writeFile(vault.filePath(QStringLiteral("sub/b.md")), "---\nstatus: Active\n---\nB ^blk\n");
        writeFile(vault.filePath(QStringLiteral("image.png")), "png");
        writeFile(vault.filePath(QStringLiteral(".hidden.md")), "x");
        writeFile(vault.filePath(QStringLiteral(".git/x.md")), "x");
        writeFile(vault.filePath(QStringLiteral(".coeus/y.md")), "x");
        writeFile(vault.filePath(QStringLiteral(".a.md.coeus-tmp-ab12")), "x");

        QCOMPARE(Indexer::noteFiles(vault.path()), (QStringList{QStringLiteral("a.md"), QStringLiteral("sub/b.md")}));

        auto store = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*store);
        const auto report = indexer.scan({0, QStringLiteral("vault"), vault.path()});
        QVERIFY2(report.has_value(), qPrintable(report.error()));
        QCOMPARE(report->added, 2);
        QCOMPARE(count(*store, "SELECT count(*) FROM notes"), 2);
        QCOMPARE(count(*store, "SELECT count(*) FROM tags"), 1);
        QCOMPARE(count(*store, "SELECT count(*) FROM headings"), 1);
        QCOMPARE(count(*store, "SELECT count(*) FROM blocks"), 1);
        QCOMPARE(count(*store, "SELECT count(*) FROM props WHERE key = 'status'"), 1);
        QCOMPARE(targetOf(*store, QStringLiteral("0:a.md"), QStringLiteral("b")), QStringLiteral("0:sub/b.md"));

        // A second scan of an unchanged vault changes nothing.
        const auto again = indexer.scan({0, QStringLiteral("vault"), vault.path()});
        QCOMPARE(again->unchanged, 2);
        QCOMPARE(again->added + again->updated + again->removed, 0);
    }

    void fullTextSearchWithSnippets()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("titan.md")), "# Coeus\n\nThe Titan of intellect and the celestial axis.\n");
        auto store = IndexStore::open(QStringLiteral(":memory:"));
        QVERIFY(Indexer(*store).scan({0, QStringLiteral("vault"), vault.path()}).has_value());

        auto q = store->database().prepare(
            "SELECT path, snippet(fts, 1, '[', ']', '…', 4) FROM fts WHERE fts MATCH ? ORDER BY rank");
        q->bind(1, QStringLiteral("celestial"));
        QVERIFY(q->step().value());
        QCOMPARE(q->text(0), QStringLiteral("titan.md"));
        QVERIFY(q->text(1).contains(QStringLiteral("[celestial]"))); // BUG-002: snippets work
    }

    void wikilinkResolutionOrder()
    {
        QTemporaryDir vault;
        QTemporaryDir linked;
        // Every project has a README and DECISIONS (AV-006).
        writeFile(vault.filePath(QStringLiteral("projects/coeus/README.md")), "x");
        writeFile(vault.filePath(QStringLiteral("projects/coeus/DECISIONS.md")),
                  "[[README]] [[projects/pontus/README]] [[pontus/README]] [[Shared]] [[Lonely]] "
                  "[[The Pole]] [[Nowhere]] [[repo:Remote]] [[photo.png]] [[README.md]]\n");
        writeFile(vault.filePath(QStringLiteral("projects/pontus/README.md")), "x");
        writeFile(vault.filePath(QStringLiteral("projects/notes/Shared.md")), "x");
        writeFile(vault.filePath(QStringLiteral("archive/Shared.md")), "x");
        writeFile(vault.filePath(QStringLiteral("top.md")), "[[Shared]] [[DECISIONS]]\n");
        writeFile(vault.filePath(QStringLiteral("pole.md")), "---\naliases: [The Pole]\n---\n");
        writeFile(linked.filePath(QStringLiteral("Lonely.md")), "x");
        writeFile(linked.filePath(QStringLiteral("Remote.md")), "x");

        auto store = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*store);
        QVERIFY(indexer.scan({1, QStringLiteral("repo"), linked.path()}).has_value());
        QVERIFY(indexer.scan({0, QStringLiteral("vault"), vault.path()}).has_value());

        const QString d = QStringLiteral("0:projects/coeus/DECISIONS.md");
        // 2. same folder beats the other project's README.
        QCOMPARE(targetOf(*store, d, QStringLiteral("README")), QStringLiteral("0:projects/coeus/README.md"));
        QCOMPARE(targetOf(*store, d, QStringLiteral("README.md")), QStringLiteral("0:projects/coeus/README.md"));
        // 1. root-relative path; partial paths match by suffix.
        QCOMPARE(targetOf(*store, d, QStringLiteral("projects/pontus/README")), QStringLiteral("0:projects/pontus/README.md"));
        QCOMPARE(targetOf(*store, d, QStringLiteral("pontus/README")), QStringLiteral("0:projects/pontus/README.md"));
        // 3. nearest folder: projects/notes shares "projects" with the source.
        QCOMPARE(targetOf(*store, d, QStringLiteral("Shared")), QStringLiteral("0:projects/notes/Shared.md"));
        // ...but from the vault root both are equally near: ambiguous, not guessed.
        QCOMPARE(targetOf(*store, QStringLiteral("0:top.md"), QStringLiteral("Shared")), QStringLiteral("<ambiguous>"));
        QCOMPARE(targetOf(*store, QStringLiteral("0:top.md"), QStringLiteral("DECISIONS")),
                 QStringLiteral("0:projects/coeus/DECISIONS.md"));
        // 4. other root. 1b. explicit root:path.
        QCOMPARE(targetOf(*store, d, QStringLiteral("Lonely")), QStringLiteral("1:Lonely.md"));
        QCOMPARE(targetOf(*store, d, QStringLiteral("repo:Remote")), QStringLiteral("1:Remote.md"));
        // 5. alias. 6. unresolved. Attachments are never resolved to notes.
        QCOMPARE(targetOf(*store, d, QStringLiteral("The Pole")), QStringLiteral("0:pole.md"));
        QCOMPARE(targetOf(*store, d, QStringLiteral("Nowhere")), QStringLiteral("<unresolved>"));
        QCOMPARE(targetOf(*store, d, QStringLiteral("photo.png")), QStringLiteral("<unresolved>"));
        QVERIFY(LinkResolver::isAttachment(QStringLiteral("photo.png")));
        QVERIFY(!LinkResolver::isAttachment(QStringLiteral("v1.2 notes")));
        QVERIFY(LinkResolver::isFileLink(true, QStringLiteral("../src/main.cpp")));
        QVERIFY(!LinkResolver::isFileLink(true, QStringLiteral("other.md")));
        QVERIFY(!LinkResolver::isFileLink(true, QStringLiteral("other")));
        QVERIFY(!LinkResolver::isFileLink(false, QStringLiteral("v1.2 notes")));
    }

    void markdownLinkResolution()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("docs/guide.md")),
                  "[up](../README.md) [same](other.md) [abs](/docs/other.md) [noext](other) "
                  "[out](../../escape.md) [missing](gone.md)\n");
        writeFile(vault.filePath(QStringLiteral("docs/other.md")), "x");
        writeFile(vault.filePath(QStringLiteral("README.md")), "[Guide](docs/guide.md)\n");

        auto store = IndexStore::open(QStringLiteral(":memory:"));
        QVERIFY(Indexer(*store).scan({0, QStringLiteral("vault"), vault.path()}).has_value());
        const QString g = QStringLiteral("0:docs/guide.md");
        QCOMPARE(targetOf(*store, g, QStringLiteral("../README.md")), QStringLiteral("0:README.md"));
        QCOMPARE(targetOf(*store, g, QStringLiteral("other.md")), QStringLiteral("0:docs/other.md"));
        QCOMPARE(targetOf(*store, g, QStringLiteral("/docs/other.md")), QStringLiteral("0:docs/other.md"));
        QCOMPARE(targetOf(*store, g, QStringLiteral("other")), QStringLiteral("0:docs/other.md"));
        QCOMPARE(targetOf(*store, g, QStringLiteral("../../escape.md")), QStringLiteral("<unresolved>"));
        QCOMPARE(targetOf(*store, g, QStringLiteral("gone.md")), QStringLiteral("<unresolved>"));
        QCOMPARE(targetOf(*store, QStringLiteral("0:README.md"), QStringLiteral("docs/guide.md")), g);
    }

    void updateHandlesCreateChangeDeleteAndResolution()
    {
        QTemporaryDir vault;
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        writeFile(vault.filePath(QStringLiteral("a.md")), "[[b]]\n");
        auto store = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(root).has_value());
        QCOMPARE(targetOf(*store, QStringLiteral("0:a.md"), QStringLiteral("b")), QStringLiteral("<unresolved>"));

        writeFile(vault.filePath(QStringLiteral("b.md")), "# B\n");
        QVERIFY(indexer.update(root, QStringLiteral("b.md")).has_value());
        QCOMPARE(targetOf(*store, QStringLiteral("0:a.md"), QStringLiteral("b")), QStringLiteral("0:b.md"));

        writeFile(vault.filePath(QStringLiteral("b.md")), "# B changed #new\n");
        QVERIFY(indexer.update(root, QStringLiteral("b.md")).has_value());
        QCOMPARE(count(*store, "SELECT count(*) FROM tags WHERE tag = 'new'"), 1);
        QCOMPARE(targetOf(*store, QStringLiteral("0:a.md"), QStringLiteral("b")), QStringLiteral("0:b.md"));

        QVERIFY(QFile::remove(vault.filePath(QStringLiteral("b.md"))));
        QVERIFY(indexer.update(root, QStringLiteral("b.md")).has_value());
        QCOMPARE(count(*store, "SELECT count(*) FROM notes"), 1);
        QCOMPARE(count(*store, "SELECT count(*) FROM tags"), 0);
        QCOMPARE(count(*store, "SELECT count(*) FROM fts"), 1);
        QCOMPARE(targetOf(*store, QStringLiteral("0:a.md"), QStringLiteral("b")), QStringLiteral("<unresolved>"));

        // Paths that are not notes are ignored.
        writeFile(vault.filePath(QStringLiteral(".git/c.md")), "x");
        QVERIFY(indexer.update(root, QStringLiteral(".git/c.md")).has_value());
        QVERIFY(indexer.update(root, QStringLiteral("picture.png")).has_value());
        QCOMPARE(count(*store, "SELECT count(*) FROM notes"), 1);
    }

    void verifyReportsDrift()
    {
        QTemporaryDir vault;
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        writeFile(vault.filePath(QStringLiteral("keep.md")), "x");
        writeFile(vault.filePath(QStringLiteral("edit.md")), "x");
        writeFile(vault.filePath(QStringLiteral("drop.md")), "x");
        auto store = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(root).has_value());
        QVERIFY(indexer.verify(root)->clean());

        // Changes the index never heard about (missed watcher events, AV-001).
        writeFile(vault.filePath(QStringLiteral("edit.md")), "changed");
        QVERIFY(QFile::remove(vault.filePath(QStringLiteral("drop.md"))));
        writeFile(vault.filePath(QStringLiteral("new.md")), "x");

        const auto report = indexer.verify(root);
        QCOMPARE(report->stale, QStringList{QStringLiteral("edit.md")});
        QCOMPARE(report->notOnDisk, QStringList{QStringLiteral("drop.md")});
        QCOMPARE(report->notIndexed, QStringList{QStringLiteral("new.md")});

        QVERIFY(indexer.scan(root).has_value());
        QVERIFY(indexer.verify(root)->clean());
    }

    void unreadableFileKeepsItsRows()
    {
        if (::geteuid() == 0)
            QSKIP("root reads everything");
        QTemporaryDir vault;
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        const QString path = vault.filePath(QStringLiteral("locked.md"));
        writeFile(path, "# Locked\n");
        auto store = IndexStore::open(QStringLiteral(":memory:"));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(root).has_value());
        QFile::setPermissions(path, QFileDevice::Permissions());
        const auto report = indexer.scan(root);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(report->failures, QStringList{QStringLiteral("locked.md")});
        QCOMPARE(count(*store, "SELECT count(*) FROM headings"), 1);
    }
};

QTEST_GUILESS_MAIN(TestIndexer)
#include "test_indexer.moc"
