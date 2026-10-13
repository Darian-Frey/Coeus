// F-006 acceptance: results within 100 ms on a 10,000-note vault.
// Builds a synthetic index directly through IndexStore (no files, no
// parsing), with bodies of about 200 words drawn from a 3,000-word
// vocabulary, then times representative queries.

#include "index/ContentHash.hpp"
#include "index/IndexStore.hpp"
#include "index/Search.hpp"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

class TestSearchSpeed : public QObject {
    Q_OBJECT

private slots:
    void tenThousandNotesUnderBudget()
    {
        constexpr int kNotes = 10000;
        constexpr int kWords = 200;
        constexpr qint64 kBudgetMs = 100;

        QRandomGenerator random(20261013);
        QStringList vocabulary;
        for (int i = 0; i < 3000; ++i)
            vocabulary.append(QStringLiteral("w%1x%2").arg(i).arg(QChar(u'a' + i % 26)));
        vocabulary.append({QStringLiteral("vault"), QStringLiteral("index"), QStringLiteral("decision"),
                           QStringLiteral("folder"), QStringLiteral("register")});

        QTemporaryDir dir;
        const QString index = dir.filePath(QStringLiteral("index.db"));
        {
            auto store = IndexStore::open(index);
            QVERIFY(store.has_value());
            QVERIFY(store->putRoot({0, QStringLiteral("vault"), dir.path()}).has_value());
            auto tx = Transaction::begin(store->database(), Transaction::Kind::Immediate);
            for (int n = 0; n < kNotes; ++n) {
                QStringList body;
                for (int w = 0; w < kWords; ++w)
                    body.append(vocabulary[random.bounded(int(vocabulary.size()))]);
                ParsedNote note;
                note.plainText = body.join(QLatin1Char(' '));
                note.tags.append({n % 7 == 0 ? QStringLiteral("project/coeus") : QStringLiteral("misc"), 1});
                const QString path = QStringLiteral("folder%1/note %2.md").arg(n % 100).arg(n);
                QVERIFY(store->putNote({0, path, 0, note.plainText.size(), ContentHash::of(note.plainText.toUtf8())}, note)
                            .has_value());
            }
            QVERIFY(tx->commit().has_value());
        }

        const QStringList queries{
            QStringLiteral("vault"),                     // common word
            QStringLiteral("w12"),                       // prefix matching many words
            QStringLiteral("vault decision -register"),  // AND with exclusion
            QStringLiteral("\"vault index\""),           // phrase
            QStringLiteral("folder tag:project"),        // full text with tag
            QStringLiteral("path:folder4 file:note 4"),  // structural filters, no full text
            QStringLiteral("re:/w99\\d/ vault"),         // regex after narrowing
        };
        Search::run(index, SearchQuery::parse(QStringLiteral("warm"))); // page cache, as in real use
        for (const QString& query : queries) {
            QElapsedTimer timer;
            timer.start();
            const auto hits = Search::run(index, SearchQuery::parse(query));
            const qint64 elapsed = timer.elapsed();
            QVERIFY2(hits.has_value(), qPrintable(hits.error()));
            qInfo("%-34s %4lld hits  %3lld ms", qPrintable(query), qint64(hits->size()), elapsed);
            QVERIFY2(elapsed < kBudgetMs, qPrintable(QStringLiteral("%1 took %2 ms").arg(query).arg(elapsed)));
        }
    }
};

QTEST_GUILESS_MAIN(TestSearchSpeed)
#include "test_search_speed.moc"
