// F-006 in the window: typing searches, results follow the index, a hit
// opens with its match selected, and Ctrl+Shift+F focuses the box.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SearchPanel.hpp"

#include <QAction>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace coeus;

namespace {

constexpr int kWaitMs = 5000;

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QStringList hits(SearchPanel* panel)
{
    QStringList out;
    for (int i = 0; i < panel->results()->topLevelItemCount(); ++i)
        out.append(panel->results()->topLevelItem(i)->text(0));
    return out;
}

} // namespace

class TestSearchPanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void searchesOpensAndFollows()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "First line.\n\nThe celestial axis turns.\n");
        write(vault.filePath(QStringLiteral("b.md")), "Nothing here.\n");

        MainWindow window;
        window.show();
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(kWaitMs));

        SearchPanel* panel = window.search();
        QSignalSpy finished(panel, &SearchPanel::finished);
        QTest::keyClicks(panel->input(), QStringLiteral("celest"));
        QVERIFY(finished.wait(kWaitMs));
        QCOMPARE(hits(panel), QStringList{QStringLiteral("a.md")});
        QVERIFY(panel->status()->text().contains(QStringLiteral("1 result")));

        // A hit opens the note with its match selected.
        emit panel->results()->itemClicked(panel->results()->topLevelItem(0), 0);
        Editor* editor = window.currentEditor();
        QCOMPARE(editor->path(), vault.filePath(QStringLiteral("a.md")));
        QCOMPARE(editor->textCursor().selectedText().toLower(), QStringLiteral("celest"));
        QCOMPARE(editor->textCursor().blockNumber(), 2);

        // Results follow the index.
        write(vault.filePath(QStringLiteral("c.md")), "Another celestial note.\n");
        QTRY_COMPARE_WITH_TIMEOUT(hits(panel).size(), 2, kWaitMs);
    }

    void errorsShowInTheStatusLine()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "x\n");
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(kWaitMs));
        QSignalSpy finished(window.search(), &SearchPanel::finished);
        window.search()->setQuery(QStringLiteral("re:/([/"));
        QVERIFY(finished.wait(kWaitMs) || !finished.isEmpty());
        QVERIFY(window.search()->status()->text().contains(QStringLiteral("re:/")));
        QCOMPARE(window.search()->results()->topLevelItemCount(), 0);
    }

    void shortcutFocusesTheSearchBox()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (QAction* action : window.registeredActions()) {
            if (action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F))
                action->trigger();
        }
        QTRY_VERIFY(window.search()->input()->hasFocus());
    }
};

QTEST_MAIN(TestSearchPanel)
#include "test_search_panel.moc"
