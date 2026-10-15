// F-014: the bookmarks and recent-notes files kept per machine in .coeus/.

#include "vault/Bookmarks.hpp"
#include "vault/RecentNotes.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

class TestBookmarks : public QObject {
    Q_OBJECT

private slots:
    void bookmarksRoundTripInOrder()
    {
        QTemporaryDir vault;
        QVERIFY(Bookmarks::load(vault.path()).isEmpty()); // no file yet
        const QList<Bookmark> saved{Bookmark::note(QStringLiteral("vault"), QStringLiteral("projects/coeus/SPEC.md")),
                                    Bookmark::search(QStringLiteral("atomic save"), QStringLiteral("meaning")),
                                    Bookmark::note(QStringLiteral("pontus"), QStringLiteral("DECISIONS.md"))};
        QVERIFY(Bookmarks::save(vault.path(), saved));
        QVERIFY(QFile::exists(vault.filePath(QStringLiteral(".coeus/bookmarks.json"))));
        QCOMPARE(Bookmarks::load(vault.path()), saved);
    }

    void togglingAddsAtTheEndOrRemoves()
    {
        const Bookmark a = Bookmark::note(QStringLiteral("vault"), QStringLiteral("a.md"));
        const Bookmark b = Bookmark::search(QStringLiteral("b"), QStringLiteral("words"));
        QList<Bookmark> list = Bookmarks::toggled({}, a);
        list = Bookmarks::toggled(list, b);
        QCOMPARE(list, (QList<Bookmark>{a, b}));
        QCOMPARE(Bookmarks::toggled(list, a), QList<Bookmark>{b});
        // The same text in another mode is another search.
        QCOMPARE(Bookmarks::toggled(list, Bookmark::search(QStringLiteral("b"), QStringLiteral("both"))).size(), 3);
    }

    void unreadableEntriesAreSkippedNotTheFile()
    {
        QTemporaryDir vault;
        QDir().mkpath(vault.filePath(QStringLiteral(".coeus")));
        QFile file(vault.filePath(QStringLiteral(".coeus/bookmarks.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"bookmarks":[{"kind":"note","root":"vault","path":"a.md"},{"kind":"note","path":"no-root.md"},
                   {"kind":"search","query":"   "},{"kind":"weird"},{"kind":"search","query":"kept"}]})");
        file.close();
        const QList<Bookmark> loaded = Bookmarks::load(vault.path());
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded[1], Bookmark::search(QStringLiteral("kept"), QStringLiteral("words"))); // mode defaults to words
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("not json");
        file.close();
        QVERIFY(Bookmarks::load(vault.path()).isEmpty());
    }

    void recentNotesNewestFirstOnceAndCapped()
    {
        QList<RecentNotes::Note> recent;
        for (int i = 0; i < RecentNotes::kLimit + 5; ++i)
            recent = RecentNotes::touched(recent, {QStringLiteral("vault"), QStringLiteral("n%1.md").arg(i)});
        QCOMPARE(recent.size(), RecentNotes::kLimit);
        QCOMPARE(recent.front().second, QStringLiteral("n%1.md").arg(RecentNotes::kLimit + 4));
        recent = RecentNotes::touched(recent, {QStringLiteral("vault"), QStringLiteral("n10.md")});
        QCOMPARE(recent.front().second, QStringLiteral("n10.md"));
        QCOMPARE(recent.count({QStringLiteral("vault"), QStringLiteral("n10.md")}), 1);

        QTemporaryDir vault;
        QVERIFY(RecentNotes::save(vault.path(), recent));
        QCOMPARE(RecentNotes::load(vault.path()), recent);
    }
};

QTEST_GUILESS_MAIN(TestBookmarks)
#include "test_bookmarks.moc"
