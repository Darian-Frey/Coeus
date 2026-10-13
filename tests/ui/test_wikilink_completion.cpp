#include "ui/WikilinkCompletion.hpp"

#include <QElapsedTimer>
#include <QTest>

using namespace coeus;

namespace {

NoteEntry note(const QString& root, const QString& path, const QStringList& aliases = {})
{
    const QString file = path.section(QLatin1Char('/'), -1);
    return {root, QStringLiteral("/r/") + root, path, file.chopped(3), aliases};
}

QStringList labels(const QList<Completion>& completions)
{
    QStringList out;
    for (const Completion& c : completions)
        out.append(c.label);
    return out;
}

} // namespace

class TestWikilinkCompletion : public QObject {
    Q_OBJECT

private slots:
    void ranksBetterMatchesFirst()
    {
        const QList<NoteEntry> notes{note(QStringLiteral("vault"), QStringLiteral("Architecture.md")),
                                     note(QStringLiteral("vault"), QStringLiteral("Archive notes.md")),
                                     note(QStringLiteral("vault"), QStringLiteral("Search.md"))};
        const QStringList ranked = labels(WikilinkCompletion::rank(QStringLiteral("arch"), notes, QStringLiteral("vault"), {}));
        QCOMPARE(ranked.size(), 3); // "Search" contains a-r-c-h too
        QCOMPARE(ranked.back(), QStringLiteral("Search"));
        QVERIFY(WikilinkCompletion::rank(QStringLiteral("zz"), notes, QStringLiteral("vault"), {}).isEmpty());
        QCOMPARE(WikilinkCompletion::rank(QString(), notes, QStringLiteral("vault"), {}).size(), 3);
    }

    void tiesPreferSameRootThenNearerFolder()
    {
        const QList<NoteEntry> notes{note(QStringLiteral("pontus"), QStringLiteral("README.md")),
                                     note(QStringLiteral("vault"), QStringLiteral("archive/README.md")),
                                     note(QStringLiteral("vault"), QStringLiteral("projects/coeus/README.md"))};
        const auto ranked = WikilinkCompletion::rank(QStringLiteral("readme"), notes, QStringLiteral("vault"),
                                                     QStringLiteral("projects/coeus"));
        QCOMPARE(ranked[0].detail, QStringLiteral("projects/coeus"));
        QCOMPARE(ranked[1].detail, QStringLiteral("archive"));
        QCOMPARE(ranked[2].detail, QStringLiteral("pontus"));
    }

    void linkTextAlwaysResolvesToTheChosenNote()
    {
        // AV-006: a shared title gets a path; a unique one stays bare.
        const QList<NoteEntry> notes{note(QStringLiteral("vault"), QStringLiteral("projects/coeus/README.md")),
                                     note(QStringLiteral("vault"), QStringLiteral("projects/pontus/README.md")),
                                     note(QStringLiteral("pontus"), QStringLiteral("README.md")),
                                     note(QStringLiteral("vault"), QStringLiteral("Unique.md"))};
        const QString vault = QStringLiteral("vault");
        QCOMPARE(WikilinkCompletion::linkText(notes[0], notes, vault), QStringLiteral("projects/coeus/README"));
        QCOMPARE(WikilinkCompletion::linkText(notes[2], notes, vault), QStringLiteral("pontus:README"));
        QCOMPARE(WikilinkCompletion::linkText(notes[3], notes, vault), QStringLiteral("Unique"));
    }

    void aliasesInsertTitleAndAlias()
    {
        const QList<NoteEntry> notes{note(QStringLiteral("vault"), QStringLiteral("Coeus.md"), {QStringLiteral("The Pole")})};
        const auto ranked = WikilinkCompletion::rank(QStringLiteral("pole"), notes, QStringLiteral("vault"), {});
        QCOMPARE(ranked.size(), 1);
        QCOMPARE(ranked[0].label, QStringLiteral("The Pole"));
        QCOMPARE(ranked[0].insert, QStringLiteral("Coeus|The Pole"));
        QCOMPARE(ranked[0].detail, QStringLiteral("→ Coeus"));
    }

    void headingsFilterAndDeduplicate()
    {
        const QStringList headings{QStringLiteral("Context"), QStringLiteral("Options"), QStringLiteral("Consequences"),
                                   QStringLiteral("Options")};
        QCOMPARE(labels(WikilinkCompletion::headings(QStringLiteral("con"), headings)),
                 (QStringList{QStringLiteral("Context"), QStringLiteral("Consequences")}));
        QCOMPARE(WikilinkCompletion::headings(QString(), headings).size(), 3);
    }

    void tenThousandNotesRankQuickly()
    {
        QList<NoteEntry> notes;
        for (int i = 0; i < 10000; ++i)
            notes.append(note(QStringLiteral("vault"), QStringLiteral("folder%1/note %2.md").arg(i % 50).arg(i)));
        QElapsedTimer timer;
        timer.start();
        const auto ranked = WikilinkCompletion::rank(QStringLiteral("no 99"), notes, QStringLiteral("vault"), QStringLiteral("folder3"));
        const qint64 elapsed = timer.elapsed();
        QCOMPARE(ranked.size(), WikilinkCompletion::kMaxResults);
        qInfo("10,000 notes ranked in %lld ms", elapsed);
        QVERIFY2(elapsed < 100, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
    }
};

QTEST_GUILESS_MAIN(TestWikilinkCompletion)
#include "test_wikilink_completion.moc"
