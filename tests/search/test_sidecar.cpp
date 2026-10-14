// F-035: the embeddings sidecar, its format, import, export, conflict copies
// and compaction (AV-007, AV-011). Only the last case needs the model.

#include "../index/IndexDump.hpp"

#include "index/IndexService.hpp"
#include "index/IndexStore.hpp"
#include "index/Indexer.hpp"
#include "search/EmbeddingService.hpp"
#include "search/Sidecar.hpp"
#include "search/VectorStore.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

const QString kModel = QStringLiteral("test-model");
constexpr int kDim = 4;

// A vector made from its hash, so either machine would compute the same one.
QByteArray vectorFor(const QByteArray& hash, int dim = kDim, float salt = 0)
{
    QVector<float> v(dim);
    for (int i = 0; i < dim; ++i)
        v[i] = float(quint8(hash[i % hash.size()])) / 255.0f + salt;
    return VectorStore::pack(v);
}

QList<QByteArray> chunkHashes(Database& db)
{
    QList<QByteArray> result;
    auto query = db.prepare("SELECT DISTINCT hash FROM chunks ORDER BY hash");
    while (query->step().value_or(false))
        result.append(query->blob(0));
    return result;
}

QHash<QByteArray, QByteArray> vectors(Database& db)
{
    QHash<QByteArray, QByteArray> result;
    auto query = db.prepare("SELECT hash, vec FROM vectors");
    while (query->step().value_or(false))
        result.insert(query->blob(0), query->blob(1));
    return result;
}

void setMeta(Database& db, const QString& key, const QString& value)
{
    auto query = db.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES (?, ?)");
    QVERIFY(query->bind(1, key).bind(2, value).run());
}

// One machine's index over `vault`, its vectors those of kModel.
struct Machine {
    std::optional<IndexStore> store;
    RootInfo root;

    Machine(const QString& vault, const QString& indexPath)
        : root{0, QStringLiteral("vault"), vault}
    {
        QDir().mkpath(QFileInfo(indexPath).absolutePath());
        auto opened = IndexStore::open(indexPath);
        store.emplace(std::move(*opened));
        store->putRoot(root);
        Indexer(*store).scan(root);
        setMeta(db(), QStringLiteral("embed_model"), kModel);
        setMeta(db(), QStringLiteral("embed_dim"), QString::number(kDim));
    }
    Database& db() { return store->database(); }
    void embedAll()
    {
        auto insert = db().prepare("INSERT OR IGNORE INTO vectors(hash, vec) VALUES (?, ?)");
        for (const QByteArray& hash : chunkHashes(db())) {
            insert->reset();
            insert->bind(1, hash).bind(2, vectorFor(hash)).run();
        }
    }
    void update(const QString& path) { Indexer(*store).update(root, path); }
};

void writeSidecar(const QString& file, const Sidecar::Contents& contents)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile out(file);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(Sidecar::serialise(contents));
}

} // namespace

class TestSidecar : public QObject {
    Q_OBJECT

private slots:
    void formatRoundTripsAndRejectsDamage()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("embeddings.bin"));
        Sidecar::Contents contents{kModel, kDim, {}};
        contents.vectors.insert(QByteArray(16, 'a'), vectorFor(QByteArray(16, 'a')));
        contents.vectors.insert(QByteArray(16, 'b'), vectorFor(QByteArray(16, 'b')));
        const QByteArray bytes = Sidecar::serialise(contents);
        QCOMPARE(bytes.left(8), QByteArray("COEUSVEC"));
        QCOMPARE(bytes.size(), 8 + 4 + 4 + kModel.size() + 4 + 8 + 2 * (16 + kDim * 4));
        QCOMPARE(Sidecar::serialise(contents), bytes); // the same vectors make the same file
        writeSidecar(file, contents);
        auto read = Sidecar::read(file);
        QVERIFY2(read, read ? "" : qPrintable(read.error()));
        QCOMPARE(read->model, kModel);
        QCOMPARE(read->dim, kDim);
        QCOMPARE(read->vectors, contents.vectors);

        const auto damaged = [&](QByteArray data) {
            QFile out(file);
            out.open(QIODevice::WriteOnly);
            out.write(data);
            out.close();
            return Sidecar::read(file);
        };
        QVERIFY(!damaged(bytes.left(bytes.size() - 3))); // cut short, as by an interrupted sync
        QVERIFY(!damaged(bytes + "x"));
        QVERIFY(!damaged(QByteArray("NOTVECTO") + bytes.mid(8)));
        QByteArray later = bytes;
        later[8] = 2; // version 2
        QVERIFY(damaged(later).error().contains(QLatin1String("version 2")));
        QVERIFY(!damaged(QByteArray()));
    }

    void secondMachineImportsInsteadOfEmbedding()
    {
        QTemporaryDir vault, elsewhere;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note.\n\n## More\n\nA second section.\n");
        writeFile(vault.filePath(QStringLiteral("b.md")), "# B\n\nSecond note.\n");
        Machine desktop(vault.path(), elsewhere.filePath(QStringLiteral("desktop.db")));
        desktop.embedAll();
        const auto exported = Sidecar::exportTo(desktop.db(), vault.path(), kModel, kDim);
        QVERIFY2(exported, exported ? "" : qPrintable(exported.error()));
        QVERIFY(exported->written);
        QCOMPARE(exported->vectors, 3);
        QVERIFY(QFile::exists(vault.filePath(QStringLiteral(".coeus-shared/embeddings.bin"))));
        // Nothing new: the file is left alone.
        QVERIFY(!Sidecar::exportTo(desktop.db(), vault.path(), kModel, kDim)->written);

        Machine laptop(vault.path(), elsewhere.filePath(QStringLiteral("laptop.db")));
        QCOMPARE(VectorStore::coverage(laptop.db())->embedded, 0);
        const auto imported = Sidecar::import(laptop.db(), vault.path(), kModel, kDim);
        QVERIFY2(imported, imported ? "" : qPrintable(imported.error()));
        QVERIFY(imported->changed);
        QCOMPARE(imported->vectors, 3);
        QVERIFY(VectorStore::coverage(laptop.db())->complete());
        QCOMPARE(vectors(laptop.db()), vectors(desktop.db()));
        // Unchanged since: not read again.
        QVERIFY(!Sidecar::import(laptop.db(), vault.path(), kModel, kDim)->changed);

        // Only passages the index has, and has no vector for, are taken.
        Machine third(vault.path(), elsewhere.filePath(QStringLiteral("third.db")));
        const QByteArray own = chunkHashes(third.db()).front();
        auto insert = third.db().prepare("INSERT INTO vectors(hash, vec) VALUES (?, ?)");
        insert->bind(1, own).bind(2, vectorFor(own, kDim, 1)).run();
        QCOMPARE(Sidecar::import(third.db(), vault.path(), kModel, kDim)->vectors, 2);
        QCOMPARE(vectors(third.db()).value(own), vectorFor(own, kDim, 1));
    }

    void anotherModelIsNeverImported()
    {
        QTemporaryDir vault, elsewhere;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note.\n");
        Machine desktop(vault.path(), elsewhere.filePath(QStringLiteral("desktop.db")));
        desktop.embedAll();
        QVERIFY(Sidecar::exportTo(desktop.db(), vault.path(), kModel, kDim));

        Machine laptop(vault.path(), elsewhere.filePath(QStringLiteral("laptop.db")));
        const auto otherModel = Sidecar::import(laptop.db(), vault.path(), QStringLiteral("other-model"), kDim);
        QVERIFY(otherModel);
        QCOMPARE(otherModel->vectors, 0);
        QCOMPARE(otherModel->skipped.size(), 1);
        QVERIFY(otherModel->skipped.front().contains(QLatin1String("not imported")));
        Machine wider(vault.path(), elsewhere.filePath(QStringLiteral("wider.db")));
        QCOMPARE(Sidecar::import(wider.db(), vault.path(), kModel, 8)->vectors, 0); // same name, other dimension
        QVERIFY(vectors(wider.db()).isEmpty());

        // A damaged sidecar is not imported, and the next export replaces it.
        QFile::resize(Sidecar::path(vault.path()), 30);
        Machine fourth(vault.path(), elsewhere.filePath(QStringLiteral("fourth.db")));
        const auto damaged = Sidecar::import(fourth.db(), vault.path(), kModel, kDim);
        QCOMPARE(damaged->vectors, 0);
        QCOMPARE(damaged->skipped.size(), 1);
        const auto replaced = Sidecar::exportTo(desktop.db(), vault.path(), kModel, kDim);
        QVERIFY(replaced->written);
        QVERIFY(Sidecar::read(Sidecar::path(vault.path())));
    }

    void exportKeepsWhatOtherMachinesSharedUntilCompaction()
    {
        QTemporaryDir vault, elsewhere;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note.\n");
        Machine desktop(vault.path(), elsewhere.filePath(QStringLiteral("desktop.db")));
        desktop.embedAll();
        // The laptop shared a passage this machine has not seen yet (not synced, or a root it lacks).
        Sidecar::Contents laptops{kModel, kDim, {}};
        const QByteArray foreign(16, 'z');
        laptops.vectors.insert(foreign, vectorFor(foreign));
        writeSidecar(Sidecar::path(vault.path()), laptops);

        const auto exported = Sidecar::exportTo(desktop.db(), vault.path(), kModel, kDim);
        QVERIFY(exported->written);
        QCOMPARE(exported->vectors, 2);
        QVERIFY(Sidecar::read(Sidecar::path(vault.path()))->vectors.contains(foreign));

        // An edit leaves the old passage's vector in the file until compaction.
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note, edited.\n");
        desktop.update(QStringLiteral("a.md"));
        desktop.embedAll();
        VectorStore::prune(desktop.db());
        QCOMPARE(Sidecar::exportTo(desktop.db(), vault.path(), kModel, kDim)->vectors, 3);
        const auto compacted = Sidecar::compact(desktop.db(), vault.path(), kModel, kDim);
        QVERIFY(compacted->written);
        QCOMPARE(compacted->vectors, 1);
        const auto left = Sidecar::read(Sidecar::path(vault.path()));
        QCOMPARE(left->vectors.keys(), chunkHashes(desktop.db()));
    }

    void conflictCopiesAreUnitedAndRemoved()
    {
        QTemporaryDir vault, elsewhere;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note.\n");
        writeFile(vault.filePath(QStringLiteral("b.md")), "# B\n\nSecond note.\n");
        Machine laptop(vault.path(), elsewhere.filePath(QStringLiteral("laptop.db")));
        const QList<QByteArray> hashes = chunkHashes(laptop.db());
        QCOMPARE(hashes.size(), 2);
        // Two machines exported at once; the sync tool kept both files.
        Sidecar::Contents mine{kModel, kDim, {}}, theirs{kModel, kDim, {}};
        mine.vectors.insert(hashes[0], vectorFor(hashes[0]));
        theirs.vectors.insert(hashes[1], vectorFor(hashes[1]));
        const QByteArray extra(16, 'q');
        theirs.vectors.insert(extra, vectorFor(extra));
        writeSidecar(Sidecar::path(vault.path()), mine);
        const QString copy = vault.filePath(QStringLiteral(".coeus-shared/embeddings.bin.conflict1"));
        writeSidecar(copy, theirs);
        const QString broken = vault.filePath(QStringLiteral(".coeus-shared/embeddings.conflict2.bin"));
        writeFile(broken, "COEUSVEC truncated");
        writeFile(vault.filePath(QStringLiteral(".coeus-shared/notes.txt")), "not a copy");
        QCOMPARE(Sidecar::conflictCopies(vault.path()), (QStringList{copy, broken}));

        const auto imported = Sidecar::import(laptop.db(), vault.path(), kModel, kDim);
        QCOMPARE(imported->vectors, 2); // one from each
        QCOMPARE(imported->skipped.size(), 1); // the broken one
        QVERIFY(VectorStore::coverage(laptop.db())->complete());

        const auto exported = Sidecar::exportTo(laptop.db(), vault.path(), kModel, kDim);
        QVERIFY(exported->written);
        QCOMPARE(exported->merged, 1);
        QCOMPARE(exported->vectors, 3); // the other machine's extra passage is kept
        QVERIFY(!QFile::exists(copy));
        QVERIFY(QFile::exists(broken)); // never deleted unread
        QCOMPARE(Sidecar::read(Sidecar::path(vault.path()))->vectors.size(), 3);
    }

    void compactionComesWeekly()
    {
        QTemporaryDir vault, elsewhere;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note.\n");
        Machine machine(vault.path(), elsewhere.filePath(QStringLiteral("index.db")));
        QVERIFY(!Sidecar::compactionDue(machine.db(), vault.path())); // the clock starts
        QVERIFY(!Sidecar::compactionDue(machine.db(), vault.path()));
        setMeta(machine.db(), QStringLiteral("sidecar_compacted"),
                QDateTime::currentDateTimeUtc().addDays(-8).toString(Qt::ISODate));
        QVERIFY(Sidecar::compactionDue(machine.db(), vault.path()));
        writeFile(vault.filePath(QStringLiteral(".coeus/config.toml")), "[semantic]\nsidecar_compact_days = 10\n");
        QVERIFY(!Sidecar::compactionDue(machine.db(), vault.path()));
        writeFile(vault.filePath(QStringLiteral(".coeus/config.toml")), "[semantic]\nsidecar_compact_days = 0\n");
        QVERIFY(!Sidecar::compactionDue(machine.db(), vault.path())); // off
        QVERIFY(Sidecar::compact(machine.db(), vault.path(), kModel, kDim));
        writeFile(vault.filePath(QStringLiteral(".coeus/config.toml")), "");
        QVERIFY(!Sidecar::compactionDue(machine.db(), vault.path())); // just compacted
    }

    void workerImportsBeforeEmbedding()
    {
        if (!QFile::exists(Embedder::defaultModelPath()))
            QSKIP("No embedding model: run tools/fetch_model.py");
        QTemporaryDir vault;
        writeFile(vault.filePath(QStringLiteral("a.md")), "# A\n\nFirst note.\n\n## More\n\nA second section.\n");
        writeFile(vault.filePath(QStringLiteral("b.md")), "# B\n\nSecond note.\n");
        const QString indexPath = IndexService::indexPath(vault.path());
        QList<QByteArray> hashes;
        {
            QDir().mkpath(QFileInfo(indexPath).absolutePath());
            auto store = IndexStore::open(indexPath);
            const RootInfo root{0, QStringLiteral("vault"), vault.path()};
            store->putRoot(root);
            Indexer(*store).scan(root);
            hashes = chunkHashes(store->database());
        }
        // Another machine's vectors for every passage: deliberately not what
        // the model would make, so a re-embedded passage would show.
        constexpr int kModelDim = 384;
        Sidecar::Contents shared{QLatin1String(Embedder::kModelName), kModelDim, {}};
        for (const QByteArray& hash : std::as_const(hashes))
            shared.vectors.insert(hash, vectorFor(hash, kModelDim));
        writeSidecar(Sidecar::path(vault.path()), shared);

        EmbeddingService service;
        QSignalSpy progress(&service, &EmbeddingService::progress);
        QSignalSpy reports(&service, &EmbeddingService::sidecarReport);
        service.open(vault.path());
        service.wake();
        QVERIFY(QTest::qWaitFor([&] { return !progress.isEmpty(); }, 30'000));
        QCOMPARE(progress.front().at(0).toInt(), 3); // all three at once: imported, not embedded
        QCOMPARE(progress.front().at(1).toInt(), 3);
        QVERIFY(reports.front().front().toString().contains(QLatin1String("Imported 3")));
        {
            auto db = Database::open(indexPath);
            QCOMPARE(vectors(*db), shared.vectors);
        }

        // A new passage is embedded here and shared.
        writeFile(vault.filePath(QStringLiteral("c.md")), "# C\n\nA third note.\n");
        {
            auto store = IndexStore::open(indexPath);
            Indexer(*store).update(RootInfo{0, QStringLiteral("vault"), vault.path()}, QStringLiteral("c.md"));
        }
        const QDateTime before = QFileInfo(Sidecar::path(vault.path())).lastModified();
        progress.clear();
        service.wake();
        QVERIFY(QTest::qWaitFor([&] { return Sidecar::read(Sidecar::path(vault.path())).value_or(Sidecar::Contents{}).vectors.size() == 4; }, 30'000));
        QVERIFY(QFileInfo(Sidecar::path(vault.path())).lastModified() >= before);
        service.close();
    }
};

QTEST_GUILESS_MAIN(TestSidecar)
#include "test_sidecar.moc"
