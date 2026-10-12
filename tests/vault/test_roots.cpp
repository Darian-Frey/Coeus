#include "vault/Roots.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using coeus::Roots;

class TestRoots : public QObject {
    Q_OBJECT

private slots:
    void parsesRoots()
    {
        const auto loaded = Roots::parse("[[roots]]\n"
                                         "name = \"coeus\"\n"
                                         "path = \"/home/shane/src/coeus\"\n\n"
                                         "[[roots]]\n"
                                         "name = \"pontus\"\n"
                                         "path = \"~/src/pontus/\"\n\n"
                                         "[[roots]]\n"
                                         "name = \"near\"\n"
                                         "path = \"../near\"\n",
                                         QStringLiteral("/vaults/notes"));
        QVERIFY(loaded.problems.isEmpty());
        QCOMPARE(loaded.roots.size(), 3);
        QCOMPARE(loaded.roots[0].name, QStringLiteral("coeus"));
        QCOMPARE(loaded.roots[0].path, QStringLiteral("/home/shane/src/coeus"));
        QCOMPARE(loaded.roots[1].path, QDir::homePath() + QStringLiteral("/src/pontus"));
        QCOMPARE(loaded.roots[2].path, QStringLiteral("/vaults/near"));
    }

    void otherSettingsAreIgnored()
    {
        const auto loaded = Roots::parse("[registers]\nreview_threshold_days = 14\n", QStringLiteral("/v"));
        QVERIFY(loaded.problems.isEmpty());
        QVERIFY(loaded.roots.isEmpty());
    }

    void rejectsBadEntriesButKeepsGoodOnes()
    {
        const auto loaded = Roots::parse("[[roots]]\nname = \"ok\"\npath = \"/a\"\n"
                                         "[[roots]]\nname = \"ok\"\npath = \"/b\"\n"       // duplicate
                                         "[[roots]]\nname = \"vault\"\npath = \"/c\"\n"    // reserved
                                         "[[roots]]\nname = \"has space\"\npath = \"/d\"\n" // bad name
                                         "[[roots]]\nname = \"nopath\"\n",                  // missing path
                                         QStringLiteral("/v"));
        QCOMPARE(loaded.roots.size(), 1);
        QCOMPARE(loaded.roots[0].path, QStringLiteral("/a"));
        QCOMPARE(loaded.problems.size(), 4);
        QVERIFY(loaded.problems[0].contains(QStringLiteral("duplicate")));
        QVERIFY(loaded.problems[1].contains(QStringLiteral("reserved")));
    }

    void malformedTomlIsOneProblemWithALine()
    {
        const auto loaded = Roots::parse("[[roots]]\nname = \"a\"\npath = \n", QStringLiteral("/v"));
        QVERIFY(loaded.roots.isEmpty());
        QCOMPARE(loaded.problems.size(), 1);
        QVERIFY(loaded.problems[0].contains(QStringLiteral("line 3")));
    }

    void rootsMustBeTables()
    {
        const auto loaded = Roots::parse("roots = \"nope\"\n", QStringLiteral("/v"));
        QVERIFY(loaded.roots.isEmpty());
        QCOMPARE(loaded.problems.size(), 1);
    }

    void loadsFromVault()
    {
        QTemporaryDir vault;
        QVERIFY(Roots::load(vault.path()).problems.isEmpty()); // no config: fine
        QVERIFY(QDir(vault.path()).mkdir(QStringLiteral(".coeus")));
        QFile config(Roots::configPath(vault.path()));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("[[roots]]\nname = \"x\"\npath = \"/x\"\n");
        config.close();
        QCOMPARE(Roots::load(vault.path()).roots.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestRoots)
#include "test_roots.moc"
