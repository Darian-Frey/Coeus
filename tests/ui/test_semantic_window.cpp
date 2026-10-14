// F-018 in the window: the embedding worker fills the index in the
// background, and the search pane finds notes by meaning. Needs the model
// (tools/fetch_model.py); skipped without it.

#include "../index/IndexDump.hpp"

#include "search/Embedder.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/RelatedPanel.hpp"
#include "ui/SearchPanel.hpp"

#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

constexpr int kWaitMs = 60'000;

} // namespace

class TestSemanticWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;
    QList<QPair<int, int>> m_progress;

    QString vault(const char* relative) const { return m_vault.filePath(QLatin1String(relative)); }

    QStringList titles() const
    {
        QStringList out;
        QTreeWidget* results = m_window->findChild<SearchPanel*>()->results();
        for (int i = 0; i < results->topLevelItemCount(); ++i)
            out.append(results->topLevelItem(i)->text(0));
        return out;
    }

    void search(const QString& text, SearchPanel::Mode mode)
    {
        SearchPanel* panel = m_window->findChild<SearchPanel*>();
        QSignalSpy finished(panel, &SearchPanel::finished);
        panel->setMode(mode);
        finished.clear();
        panel->setQuery(text);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), kWaitMs);
    }

private slots:
    void initTestCase()
    {
        if (!QFile::exists(Embedder::defaultModelPath()))
            QSKIP("No embedding model: run tools/fetch_model.py");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        writeFile(vault("saving.md"), "# Saving\n\nIntro.\n\n## Crashes\n\nEach save goes to a temporary file that is synced and "
                                      "renamed over the note, so a crash or a power cut never leaves half a note.\n");
        writeFile(vault("tags.md"), "# Tags\n\nThe tag pane lists every tag with how many notes carry it.\n");
        writeFile(vault("garden.md"), "# Garden\n\nTomatoes need sun and steady watering in July.\n");
        m_window = std::make_unique<MainWindow>();
        connect(m_window.get(), &MainWindow::embeddingProgress, this, [this](int embedded, int total) { m_progress.append({embedded, total}); });
        m_window->resize(1100, 750);
        m_window->show();
        m_window->openVault(*Vault::open(m_vault.path()));
        // The worker embeds everything in the background; the window only hears progress.
        QTRY_VERIFY_WITH_TIMEOUT(!m_progress.isEmpty() && m_progress.last().first == m_progress.last().second && m_progress.last().second > 0,
                                 kWaitMs);
    }

    void theStatusBarSaysWhenMeaningIsReady()
    {
        bool shown = false;
        for (QLabel* label : m_window->statusBar()->findChildren<QLabel*>())
            shown |= label->isVisible() && label->text() == QStringLiteral("meaning ✓");
        QVERIFY(shown);
    }

    void findsByMeaningAndOpensThePassage()
    {
        search(QStringLiteral("what happens to my writing if the electricity fails"), SearchPanel::Mode::Meaning);
        QVERIFY2(!titles().isEmpty(), qPrintable(m_window->findChild<SearchPanel*>()->status()->text()));
        QCOMPARE(titles().front(), QStringLiteral("saving.md › Saving › Crashes"));
        QTreeWidget* results = m_window->findChild<SearchPanel*>()->results();
        emit results->itemClicked(results->topLevelItem(0), 0);
        Editor* editor = m_window->currentEditor();
        QCOMPARE(editor->path(), vault("saving.md"));
        QCOMPARE(editor->textCursor().blockNumber() + 1, 5); // the "## Crashes" passage starts there
    }

    void bothModesTogether()
    {
        search(QStringLiteral("tomatoes"), SearchPanel::Mode::Both);
        QCOMPARE(titles().front(), QStringLiteral("garden.md")); // by its word and its meaning
        search(QStringLiteral("tomatoes"), SearchPanel::Mode::Words);
        QCOMPARE(titles(), QStringList{QStringLiteral("garden.md")});
    }

    void relatedNotesAndLinkHere()
    {
        RelatedPanel* related = m_window->related();
        QSignalSpy updated(related, &RelatedPanel::updated);
        m_window->openNote(vault("garden.md"));
        QTRY_VERIFY_WITH_TIMEOUT(related->notePath() == vault("garden.md") && related->tree()->topLevelItemCount() == 2, kWaitMs);
        // The two other notes; garden.md links to neither yet.
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        related->linkButton(0)->click();
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral("]]")));
        const QString inserted = editor->toPlainText().section(QStringLiteral("[["), -1).chopped(2);
        QVERIFY(inserted == QStringLiteral("saving") || inserted == QStringLiteral("tags"));
        QVERIFY(m_window->saveCurrent());
        // Once indexed, the note it now links to drops off the list.
        QTRY_VERIFY_WITH_TIMEOUT(related->tree()->topLevelItemCount() == 1, kWaitMs);
        QVERIFY(!related->tree()->topLevelItem(0)->text(0).startsWith(inserted));
    }

    void anEditIsEmbeddedAgain()
    {
        m_progress.clear();
        writeFile(vault("tags.md"), "# Tags\n\nNested tags group notes about castles and moats.\n");
        QTRY_VERIFY_WITH_TIMEOUT(!m_progress.isEmpty() && m_progress.last().first == m_progress.last().second, kWaitMs);
        search(QStringLiteral("medieval fortifications"), SearchPanel::Mode::Meaning);
        QCOMPARE(titles().front(), QStringLiteral("tags.md › Tags"));
    }

    void cleanupTestCase()
    {
        m_window.reset();
    }
};

QTEST_MAIN(TestSemanticWindow)
#include "test_semantic_window.moc"
