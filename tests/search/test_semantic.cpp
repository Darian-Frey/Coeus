// F-018: chunking, the vector store and finding notes by meaning.

#include "../index/IndexDump.hpp"

#include "index/IndexService.hpp"
#include "index/IndexStore.hpp"
#include "index/Indexer.hpp"
#include "search/Chunker.hpp"
#include "search/VectorStore.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

class TestSemantic : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Embedder> m_embedder;

private slots:
    void initTestCase()
    {
        if (QFile::exists(Embedder::defaultModelPath())) {
            auto loaded = Embedder::load(Embedder::defaultModelPath());
            QVERIFY2(loaded, loaded ? "" : qPrintable(loaded.error()));
            m_embedder = std::move(*loaded);
        }
    }

    void chunksFollowHeadings()
    {
        const auto chunks = Chunker::split(QStringLiteral("---\ntags: [x]\nsecret: hidden\n---\nIntro words here.\n\n"
                                                          "# Saving\nWrite a temp file.\n\n## Rename\nThen rename it.\n\n"
                                                          "```\n# not a heading\n```\n### Deeper stays in Rename\n"),
                                           QStringLiteral("Plan"));
        QCOMPARE(chunks.size(), 3);
        QCOMPARE(chunks[0].heading, QString());
        QCOMPARE(chunks[0].startLine, 5); // after the frontmatter
        QCOMPARE(chunks[0].text, QStringLiteral("Plan\nIntro words here."));
        QVERIFY(!chunks[0].text.contains(QStringLiteral("secret")));
        QCOMPARE(chunks[1].heading, QStringLiteral("Saving"));
        QCOMPARE(chunks[1].text, QStringLiteral("Plan › Saving\n#\nSaving\nWrite a temp file.").replace(QStringLiteral("#\n"), QStringLiteral("# ")));
        QCOMPARE(chunks[2].heading, QStringLiteral("Saving › Rename"));
        QCOMPARE(chunks[2].startLine, 10);
        QCOMPARE(chunks[2].endLine, 16);
        QVERIFY(chunks[2].text.contains(QStringLiteral("# not a heading")));
        QCOMPARE(chunks[2].hash.size(), 16);
        QCOMPARE(Chunker::split(QStringLiteral("Intro words here."), QStringLiteral("Plan"))[0].hash, chunks[0].hash); // stable
    }

    void registerFilesSplitAtEachEntry()
    {
        const QString decisions = QStringLiteral("# Decisions\n\nAppend-only.\n\n### D-001 First choice\n**Status:** Accepted\n\n"
                                                 "### BUG-002: Not in a bug file, still an entry\nx\n\n```\n### D-003 In a fence\n```\n");
        const auto entries = Chunker::split(decisions, QStringLiteral("DECISIONS"), Chunker::kDefaultTokens, Chunker::kDefaultOverlap, true);
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries[0].heading, QStringLiteral("Decisions"));
        QCOMPARE(entries[1].heading, QStringLiteral("Decisions › D-001 First choice"));
        QCOMPARE(entries[1].startLine, 5);
        QCOMPARE(entries[2].heading, QStringLiteral("Decisions › BUG-002: Not in a bug file, still an entry"));
        QVERIFY(entries[2].text.contains(QStringLiteral("D-003 In a fence")));
        QCOMPARE(Chunker::split(decisions, QStringLiteral("DECISIONS")).size(), 1); // an ordinary note does not
    }

    void longSectionsBecomeOverlappingWindows()
    {
        QString text;
        for (int i = 0; i < 700; ++i)
            text += QStringLiteral("w%1%2").arg(i).arg(i % 10 == 9 ? QLatin1Char('\n') : QLatin1Char(' '));
        const auto chunks = Chunker::split(text, QStringLiteral("Long"));
        QCOMPARE(Chunker::wordsFor(384), 288);
        QCOMPARE(chunks.size(), 3); // words 0–287, 252–539, 504–699: 36 words of overlap
        QVERIFY(chunks[0].text.contains(QStringLiteral("w287")) && !chunks[0].text.contains(QStringLiteral("w288")));
        QVERIFY(chunks[1].text.contains(QStringLiteral("\nw252 ")) || chunks[1].text.contains(QStringLiteral("\nw252\n")));
        QVERIFY(chunks[2].text.endsWith(QStringLiteral("w699")));
        QCOMPARE(chunks[1].startLine, 26); // w252 is on line 26 (ten words a line)
        QVERIFY(Chunker::split(QStringLiteral("   \n\n"), QStringLiteral("Empty")).isEmpty());
    }

    void vectorsFollowTheVault()
    {
        if (!m_embedder)
            QSKIP("No embedding model: run tools/fetch_model.py");
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("atomic.md")),
                  "# Saving notes\n\nEvery save writes a temporary file, syncs it to disk and renames it over the note, so a "
                  "crash or a power cut never leaves half a note behind.\n");
        writeFile(vault.filePath(QStringLiteral("tags.md")), "# Tags\n\nThe tag pane lists every tag with how many notes carry it.\n");
        writeFile(vault.filePath(QStringLiteral("blame.md")), "# Blame\n\nBlame shows the commit that last changed each line.\n");
        const QString index = vault.filePath(QStringLiteral("index.db"));
        auto store = IndexStore::open(index);
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        QVERIFY(store->putRoot(root));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(root));

        Database& db = store->database();
        QVERIFY(VectorStore::adopt(db, *m_embedder));
        auto coverage = VectorStore::coverage(db);
        QCOMPARE(coverage->total, 3);
        QCOMPARE(coverage->embedded, 0);
        QCOMPARE(VectorStore::catchUp(db, *m_embedder, QDeadlineTimer(30'000)).value(), 3);
        QVERIFY(VectorStore::coverage(db)->complete());

        const auto query = m_embedder->embed({QStringLiteral("what protects my writing if the electricity goes off")},
                                             Embedder::Purpose::Query);
        auto hits = SemanticSearch::nearest(index, query->front(), 2);
        QVERIFY(hits);
        QCOMPARE(hits->size(), 2);
        QCOMPARE(hits->front().notePath, vault.filePath(QStringLiteral("atomic.md")));
        QCOMPARE(hits->front().heading, QStringLiteral("Saving notes"));
        QCOMPARE(hits->front().startLine, 1);
        QVERIFY(hits->front().snippet.startsWith(QStringLiteral("# Saving notes Every save")));

        // Related to tags.md: the nearest other notes, never itself.
        const auto related = SemanticSearch::related(index, vault.filePath(QStringLiteral("tags.md")), 1);
        QVERIFY(related);
        QCOMPARE(related->ownPassages, 1);
        QCOMPARE(related->ownEmbedded, 1);
        QCOMPARE(related->notes.size(), 1);
        QVERIFY(related->notes.front().notePath != vault.filePath(QStringLiteral("tags.md")));
        // A link takes a note off the list.
        writeFile(vault.filePath(QStringLiteral("blame.md")), "# Blame\n\nBlame shows the commit that last changed each line. See [[atomic]].\n");
        QVERIFY(indexer.update(root, QStringLiteral("blame.md")));
        VectorStore::embedPending(db, *m_embedder, 10);
        const auto fromBlame = SemanticSearch::related(index, vault.filePath(QStringLiteral("blame.md")), 5);
        QCOMPARE(fromBlame->notes.size(), 1); // tags.md only: atomic.md is linked, blame.md is itself
        QCOMPARE(fromBlame->notes.front().notePath, vault.filePath(QStringLiteral("tags.md")));
        VectorStore::prune(db);

        // An edit re-embeds only the passage it changed.
        writeFile(vault.filePath(QStringLiteral("tags.md")), "# Tags\n\nThe tag pane lists tags, nested on slashes.\n");
        QVERIFY(indexer.update(root, QStringLiteral("tags.md")));
        coverage = VectorStore::coverage(db);
        QCOMPARE(coverage->total, 3);
        QCOMPARE(coverage->embedded, 2);
        QCOMPARE(VectorStore::embedPending(db, *m_embedder, 100).value(), 1);
        QCOMPARE(VectorStore::prune(db).value(), 1); // the old Tags vector

        // Another model's vectors are dropped rather than compared (AV-007).
        QVERIFY(db.exec("UPDATE meta SET value = 'some-other-model' WHERE key = 'embed_model'"));
        QVERIFY(VectorStore::adopt(db, *m_embedder));
        QCOMPARE(VectorStore::coverage(db)->embedded, 0);
    }

    void boilerplateDoesNotMakeNotesRelated()
    {
        if (!m_embedder)
            QSKIP("No embedding model: run tools/fetch_model.py");
        // Every project's ARCHITECTURE.md opens with the same scaffold
        // preamble; only crash-safety.md shares this one's subject.
        QTemporaryDir vault;
        const QByteArray preamble = "# Architecture\n\nDescriptive only. Rationale lives in DECISIONS.md.\n\n";
        const QList<QByteArray> subjects{"Sourdough needs a starter fed with flour and water every day.",
                                         "Tomatoes want full sun and deep watering twice a week.",
                                         "Jupiter's moons were first seen through a telescope in 1610.",
                                         "A chess opening trades a pawn for faster development.",
                                         "Watercolour paper must be stretched before painting wet on wet.",
                                         "The marathon distance was fixed at 42.195 km in 1921.",
                                         "Bees communicate where flowers are with a waggle dance.",
                                         "A violin bow is strung with horsehair and rosin.",
                                         "Granite forms when magma cools slowly underground.",
                                         "Origami cranes are folded from a single square sheet."};
        for (qsizetype i = 0; i < subjects.size(); ++i)
            writeFile(vault.filePath(QStringLiteral("p%1/ARCHITECTURE.md").arg(i)), preamble + "## Topic\n\n" + subjects[i] + "\n");
        writeFile(vault.filePath(QStringLiteral("coeus/ARCHITECTURE.md")),
                  preamble + "## Saving\n\nEvery save writes a temporary file, fsyncs it and renames it over the note, so a crash "
                             "never leaves half a note on disk.\n");
        writeFile(vault.filePath(QStringLiteral("crash-safety.md")),
                  "# Crash safety\n\nWrites go to a temp file that is flushed and atomically renamed, so power loss cannot "
                  "truncate a document.\n");
        const QString index = vault.filePath(QStringLiteral("index.db"));
        auto store = IndexStore::open(index);
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        QVERIFY(store->putRoot(root));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(root));
        QVERIFY(VectorStore::adopt(store->database(), *m_embedder));
        QVERIFY(VectorStore::catchUp(store->database(), *m_embedder, QDeadlineTimer(60'000)));
        QVERIFY(VectorStore::coverage(store->database())->complete());

        // The preamble is near-identical in eleven notes, so on raw similarity
        // every other ARCHITECTURE.md would outrank crash-safety.md.
        const auto related = SemanticSearch::related(index, vault.filePath(QStringLiteral("coeus/ARCHITECTURE.md")), 3);
        QVERIFY(related);
        QCOMPARE(related->notes.size(), 3);
        QCOMPARE(related->notes.front().notePath, vault.filePath(QStringLiteral("crash-safety.md")));
        QVERIFY(related->notes[0].score > related->notes[1].score);
    }

    void newChunkingRulesRechunkOldNotes()
    {
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("DECISIONS.md")), "# Decisions\n\n### D-001 One\nx\n\n### D-002 Two\ny\n");
        auto store = IndexStore::open(QStringLiteral(":memory:"));
        const RootInfo root{0, QStringLiteral("vault"), vault.path()};
        QVERIFY(store->putRoot(root));
        Indexer indexer(*store);
        QVERIFY(indexer.scan(root));
        const auto count = [&store] {
            auto q = store->database().prepare("SELECT count(*) FROM chunks");
            q->step();
            return int(q->int64(0));
        };
        QCOMPARE(count(), 3);
        // As an index chunked by older rules would be: one chunk, another signature.
        QVERIFY(store->database().exec("DELETE FROM chunks WHERE ordinal > 0"));
        QVERIFY(store->database().exec("UPDATE meta SET value = '1:384:48' WHERE key = 'chunking:0'"));
        QVERIFY(indexer.scan(root)); // nothing changed on disk, yet the note is chunked again
        QCOMPARE(count(), 3);
        QVERIFY(store->database().exec("DELETE FROM chunks WHERE ordinal > 0"));
        QVERIFY(indexer.scan(root)); // the same rules now: unchanged notes are left as they are
        QCOMPARE(count(), 1);
    }

    void blendingRewardsAgreement()
    {
        using Words = QList<QPair<QString, QPair<QString, QString>>>;
        const Words words{{QStringLiteral("/a.md"), {QStringLiteral("A"), QStringLiteral("a")}},
                          {QStringLiteral("/b.md"), {QStringLiteral("B"), QStringLiteral("b")}}};
        const QList<SemanticSearch::Hit> meaning{{QStringLiteral("/c.md"), QStringLiteral("C"), QStringLiteral("H"), QStringLiteral("c.md"), 3, 4, QStringLiteral("c"), 0.9f},
                                                 {QStringLiteral("/b.md"), QStringLiteral("B"), QStringLiteral("Hb"), QStringLiteral("b.md"), 7, 8, QStringLiteral("b2"), 0.8f}};
        const auto blended = SemanticSearch::blend(words, meaning, 10);
        QCOMPARE(blended.size(), 3);
        QCOMPARE(blended[0].notePath, QStringLiteral("/b.md")); // found both ways
        QVERIFY(blended[0].byWords && blended[0].byMeaning);
        QCOMPARE(blended[0].line, 7);
        QCOMPARE(blended[0].snippet, QStringLiteral("b")); // the words' snippet, which shows the match
        QCOMPARE(SemanticSearch::blend(words, meaning, 1).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSemantic)
#include "test_semantic.moc"
