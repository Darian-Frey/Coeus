// F-018, D-008: bge-small-en-v1.5 through llama.cpp. Needs the model
// (tools/fetch_model.py); without it these tests skip, so a machine that
// has not downloaded it still builds and tests the rest.

#include "search/Embedder.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QTest>

using namespace coeus;

namespace {

float dot(const QVector<float>& a, const QVector<float>& b)
{
    float sum = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        sum += a[i] * b[i];
    return sum;
}

} // namespace

class TestEmbedder : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Embedder> m_embedder;

private slots:
    void initTestCase()
    {
        const QString path = Embedder::defaultModelPath();
        if (!QFile::exists(path))
            QSKIP("No embedding model: run tools/fetch_model.py");
        auto loaded = Embedder::load(path);
        QVERIFY2(loaded, loaded ? "" : qPrintable(loaded.error()));
        m_embedder = std::move(*loaded);
    }

    void describesItself()
    {
        QCOMPARE(m_embedder->dim(), 384);
        QCOMPARE(m_embedder->modelName(), QStringLiteral("bge-small-en-v1.5"));
        QCOMPARE(m_embedder->maxTokens(), 512);
        QVERIFY(m_embedder->countTokens(QStringLiteral("Hello world")) >= 4); // [CLS] hello world [SEP]
    }

    void vectorsAreUnitLengthAndStable()
    {
        const auto once = m_embedder->embed({QStringLiteral("The vault is a plain folder of Markdown files.")});
        QVERIFY(once);
        QCOMPARE(once->size(), 1);
        QVERIFY(std::abs(dot(once->front(), once->front()) - 1.0f) < 1e-4f);
        const auto again = m_embedder->embed({QStringLiteral("The vault is a plain folder of Markdown files.")});
        QVERIFY(dot(once->front(), again->front()) > 0.9999f);
    }

    void batchesMatchOneAtATime()
    {
        QStringList texts;
        for (int i = 0; i < 11; ++i) // more than one pass of eight
            texts.append(QStringLiteral("Note %1 talks about topic number %2 at some length.").arg(i).arg(i * 7));
        const auto batched = m_embedder->embed(texts);
        QVERIFY(batched);
        QCOMPARE(batched->size(), texts.size());
        for (qsizetype i = 0; i < texts.size(); i += 5) {
            const auto single = m_embedder->embed({texts[i]});
            QVERIFY2(dot(batched->at(i), single->front()) > 0.999f, qPrintable(texts[i]));
        }
    }

    void meaningBeatsSharedWords()
    {
        const auto passages = m_embedder->embed({
            QStringLiteral("Saves go to a temporary file, which is synced to disk and then renamed over the original, "
                           "so a crash never leaves a half-written note."),
            QStringLiteral("The tag pane lists every tag in the vault with the number of notes carrying it."),
            QStringLiteral("Blame shows who last changed each line of a note, and when."),
        });
        QVERIFY(passages);
        const auto query = m_embedder->embed({QStringLiteral("how are notes protected from corruption when the power fails")},
                                             Embedder::Purpose::Query);
        QVERIFY(query);
        const float atomic = dot(query->front(), passages->at(0));
        QVERIFY2(atomic > dot(query->front(), passages->at(1)) && atomic > dot(query->front(), passages->at(2)),
                 "the atomic-write passage should be nearest");
        // The query prefix makes a different vector from the bare text.
        const auto bare = m_embedder->embed({QStringLiteral("how are notes protected from corruption when the power fails")});
        QVERIFY(dot(query->front(), bare->front()) < 0.999f);
    }

    void longTextIsCutNotRefused()
    {
        QString text;
        for (int i = 0; i < 1500; ++i)
            text += QStringLiteral("word%1 ").arg(i);
        QVERIFY(m_embedder->countTokens(text) > 512);
        const auto vectors = m_embedder->embed({text, QStringLiteral("short")});
        QVERIFY(vectors);
        QCOMPARE(vectors->size(), 2);
    }

    void throughputOnThisMachine()
    {
        QStringList passages;
        const QString paragraph = QStringLiteral(
            "Coeus keeps a rebuildable SQLite index beside a folder of Markdown notes. The index is derived and can be "
            "deleted at any time without losing anything, because the notes themselves are the source of truth. ");
        for (int i = 0; i < 64; ++i)
            passages.append(paragraph.repeated(4) + QString::number(i)); // about 200 tokens each
        QElapsedTimer timer;
        timer.start();
        const auto vectors = m_embedder->embed(passages);
        QVERIFY(vectors);
        const double seconds = double(timer.elapsed()) / 1000.0;
        qInfo("%d passages of ~%d tokens in %.2f s: %.0f passages/s", int(passages.size()),
              m_embedder->countTokens(passages.front()), seconds, double(passages.size()) / seconds);
    }

    void missingModelSaysHowToGetIt()
    {
        const auto missing = Embedder::load(QStringLiteral("/nonexistent/model.gguf"));
        QVERIFY(!missing);
        QVERIFY(missing.error().contains(QStringLiteral("fetch_model.py")));
    }
};

QTEST_GUILESS_MAIN(TestEmbedder)
#include "test_embedder.moc"
