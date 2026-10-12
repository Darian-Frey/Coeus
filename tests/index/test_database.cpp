#include "index/ContentHash.hpp"
#include "index/Database.hpp"
#include "index/IndexStore.hpp"
#include "index/Schema.hpp"

#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

class TestDatabase : public QObject {
    Q_OBJECT

private slots:
    void bindsAndReadsEveryType()
    {
        auto db = Database::open(QStringLiteral(":memory:"));
        QVERIFY(db.has_value());
        QVERIFY(db->exec("CREATE TABLE t (i INTEGER, r REAL, s TEXT, b BLOB, n TEXT)").has_value());

        auto insert = db->prepare("INSERT INTO t VALUES (?, ?, ?, ?, ?)");
        QVERIFY(insert.has_value());
        QVERIFY(insert->bind(1, std::int64_t(1) << 40)
                    .bind(2, 2.5)
                    .bind(3, QStringLiteral("Κοῖος"))
                    .bind(4, QByteArray("\x00\x01\x02", 3))
                    .bind(5, QString())
                    .run()
                    .has_value());

        auto select = db->prepare("SELECT i, r, s, b, n FROM t");
        QVERIFY(select.has_value());
        QCOMPARE(select->columnCount(), 5);
        QCOMPARE(select->step().value(), true);
        QCOMPARE(select->int64(0), std::int64_t(1) << 40);
        QCOMPARE(select->real(1), 2.5);
        QCOMPARE(select->text(2), QStringLiteral("Κοῖος"));
        QCOMPARE(select->blob(3), QByteArray("\x00\x01\x02", 3));
        QVERIFY(select->isNull(4));
        QCOMPARE(select->step().value(), false);
    }

    void reportsErrors()
    {
        auto db = Database::open(QStringLiteral(":memory:"));
        QVERIFY(!db->exec("NOT SQL").has_value());
        QVERIFY(!db->prepare("SELECT * FROM missing").has_value());
        QVERIFY(db->exec("CREATE TABLE u (x UNIQUE)").has_value());
        QVERIFY(db->exec("INSERT INTO u VALUES (1)").has_value());
        auto duplicate = db->prepare("INSERT INTO u VALUES (1)");
        QVERIFY(!duplicate->run().has_value());
    }

    void transactionRollsBackUnlessCommitted()
    {
        auto db = Database::open(QStringLiteral(":memory:"));
        QVERIFY(db->exec("CREATE TABLE t (x)").has_value());
        {
            auto tx = Transaction::begin(*db);
            QVERIFY(db->exec("INSERT INTO t VALUES (1)").has_value());
        }
        {
            auto tx = Transaction::begin(*db, Transaction::Kind::Immediate);
            QVERIFY(db->exec("INSERT INTO t VALUES (2)").has_value());
            QVERIFY(tx->commit().has_value());
        }
        auto count = db->prepare("SELECT group_concat(x) FROM t");
        QVERIFY(count->step().value());
        QCOMPARE(count->text(0), QStringLiteral("2"));
    }

    void hasFts5AndForeignKeys()
    {
        auto db = Database::open(QStringLiteral(":memory:"));
        QVERIFY(db->exec("CREATE VIRTUAL TABLE f USING fts5(body)").has_value());
        auto fk = db->prepare("PRAGMA foreign_keys");
        QVERIFY(fk->step().value());
        QCOMPARE(fk->int64(0), std::int64_t(1));
    }

    void walOnDisk()
    {
        QTemporaryDir dir;
        auto db = Database::open(dir.filePath(QStringLiteral("index.db")));
        auto mode = db->prepare("PRAGMA journal_mode");
        QVERIFY(mode->step().value());
        QCOMPARE(mode->text(0), QStringLiteral("wal"));
    }

    void contentHashIsBlake2b128()
    {
        // Reference BLAKE2b with a 16-byte digest.
        QCOMPARE(ContentHash::of("").toHex(), QByteArray("cae66941d9efbd404e4d88758ea67670"));
        QCOMPARE(ContentHash::of("abc").toHex(), QByteArray("cf4ab791c62b8d2b2109c90275287816"));
        QCOMPARE(ContentHash::of("abc").size(), ContentHash::kBytes);
    }

    void storeCreatesSchemaOnce()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("index.db"));
        {
            auto store = IndexStore::open(path);
            QVERIFY2(store.has_value(), qPrintable(store.error()));
            QVERIFY(store->putRoot({0, QStringLiteral("vault"), dir.path()}).has_value());
        }
        auto again = IndexStore::open(path);
        QVERIFY(again.has_value());
        QCOMPARE(Schema::version(again->database()).value(), Schema::kVersion);
        auto roots = again->database().prepare("SELECT count(*) FROM roots");
        QVERIFY(roots->step().value());
        QCOMPARE(roots->int64(0), std::int64_t(1)); // kept: same version
    }

    void otherSchemaVersionIsRebuilt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("index.db"));
        {
            auto db = Database::open(path);
            QVERIFY(db->exec("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);"
                             "INSERT INTO meta VALUES ('schema_version', '999');"
                             "CREATE TABLE junk (x);")
                        .has_value());
        }
        auto store = IndexStore::open(path);
        QVERIFY2(store.has_value(), qPrintable(store.error()));
        QCOMPARE(Schema::version(store->database()).value(), Schema::kVersion);
        QVERIFY(!store->database().prepare("SELECT * FROM junk").has_value());
    }

    void unreadableFileIsRebuilt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("index.db"));
        QFile junk(path);
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(4096, 'x'));
        junk.close();
        auto store = IndexStore::open(path);
        QVERIFY2(store.has_value(), qPrintable(store.error()));
        QCOMPARE(Schema::version(store->database()).value(), Schema::kVersion);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "test_database.moc"
