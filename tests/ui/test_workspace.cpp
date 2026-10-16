// F-012 step 1: panes of tabs in splitters, one index across them, and the
// window opening notes into the focused pane without ever opening one twice.

#include "ui/BacklinksPanel.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteView.hpp"
#include "ui/VaultSession.hpp"
#include "ui/Workspace.hpp"

#include <QAction>
#include <QFile>
#include <QPointer>
#include <QLabel>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

QAction* actionNamed(MainWindow& window, const QString& text)
{
    for (QAction* action : window.registeredActions())
        if (action->text().remove(QLatin1Char('&')) == text)
            return action;
    return nullptr;
}

QStringList titles(const Workspace& workspace)
{
    QStringList list;
    for (int i = 0; i < workspace.count(); ++i)
        list.append(workspace.tabText(i));
    return list;
}

} // namespace

class TestWorkspace : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void oneIndexAcrossPanes()
    {
        Workspace workspace;
        workspace.resize(800, 600);
        workspace.show();
        auto* a = new QLabel(QStringLiteral("a"));
        auto* b = new QLabel(QStringLiteral("b"));
        auto* c = new QLabel(QStringLiteral("c"));
        workspace.addTab(a, QStringLiteral("A"));
        workspace.setCurrentIndex(workspace.addTab(b, QStringLiteral("B"))); // as the window does
        QCOMPARE(workspace.panes().size(), 1);
        QCOMPARE(workspace.currentWidget(), b);

        // Split right: B, the current tab, moves into the new pane, which becomes current.
        workspace.split(Qt::Horizontal);
        QCOMPARE(workspace.panes().size(), 2);
        QCOMPARE(workspace.paneOf(a), workspace.panes()[0]);
        QCOMPARE(workspace.paneOf(b), workspace.panes()[1]);
        QCOMPARE(workspace.currentPane(), workspace.panes()[1]);
        auto* splitter = qobject_cast<QSplitter*>(workspace.panes()[0]->parentWidget());
        QVERIFY(splitter);
        QCOMPARE(splitter->orientation(), Qt::Horizontal);
        // New tabs go to the current pane; the index runs across both.
        workspace.addTab(c, QStringLiteral("C"));
        QCOMPARE(titles(workspace), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QCOMPARE(workspace.indexOf(c), 2);
        QCOMPARE(workspace.widget(0), a);
        // Making a tab current makes its pane current.
        QSignalSpy changed(&workspace, &Workspace::currentChanged);
        workspace.setCurrentWidget(a);
        QCOMPARE(workspace.currentPane(), workspace.panes()[0]);
        QCOMPARE(workspace.currentIndex(), 0);
        QVERIFY(!changed.isEmpty());
        // Closing asks with the index across panes.
        QSignalSpy close(&workspace, &Workspace::tabCloseRequested);
        emit workspace.panes()[1]->tabCloseRequested(1); // C, second in its pane
        QCOMPARE(close.takeFirst().at(0).toInt(), 2);

        // Split down inside the right pane: a splitter of the other orientation.
        workspace.setCurrentWidget(c);
        workspace.split(Qt::Vertical);
        QCOMPARE(workspace.panes().size(), 3);
        QCOMPARE(workspace.paneOf(c), workspace.panes()[2]);
        QCOMPARE(qobject_cast<QSplitter*>(workspace.panes()[2]->parentWidget())->orientation(), Qt::Vertical);

        // Emptying a pane removes it, and a splitter left with one child gives way.
        workspace.removeTab(workspace.indexOf(c));
        QCOMPARE(workspace.panes().size(), 2);
        QTest::qWait(0);
        QCOMPARE(workspace.count(), 2);
        workspace.removeTab(workspace.indexOf(b));
        QCOMPARE(workspace.panes().size(), 1);
        QTest::qWait(0);
        QCOMPARE(workspace.findChildren<QSplitter*>().size(), 0);
        QCOMPARE(workspace.currentWidget(), a);
        // The only pane stays, even empty.
        workspace.removeTab(0);
        QCOMPARE(workspace.panes().size(), 1);
        QCOMPARE(workspace.count(), 0);
    }

    void movingTabsBetweenPanes()
    {
        Workspace workspace;
        auto* a = new QLabel;
        auto* b = new QLabel;
        workspace.addTab(a, QStringLiteral("A"));
        QVERIFY(!workspace.moveToNextPane()); // one pane: nowhere to go
        workspace.split(Qt::Horizontal);      // A is alone: it stays, the new pane is empty
        QCOMPARE(workspace.paneOf(a), workspace.panes()[0]);
        QCOMPARE(workspace.panes()[1]->count(), 0);
        workspace.addTab(b, QStringLiteral("B")); // into the new, current pane
        QCOMPARE(workspace.paneOf(b), workspace.panes()[1]);
        QVERIFY(workspace.moveToNextPane());      // B to the first pane; the second, empty, goes
        QCOMPARE(workspace.paneOf(b), workspace.panes()[0]);
        QCOMPARE(workspace.panes().size(), 1);
        QCOMPARE(workspace.currentWidget(), b);
        QCOMPARE(workspace.tabText(workspace.indexOf(b)), QStringLiteral("B"));
    }

    void notesOpenInTheFocusedPaneOnce()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "# A\n\nSee [[b]].\n");
        write(vault.filePath(QStringLiteral("b.md")), "# B\n");
        write(vault.filePath(QStringLiteral("c.md")), "# C\n\n[[b]] again.\n");
        MainWindow window;
        window.resize(1200, 700);
        window.show();
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(5000));
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        window.openNote(vault.filePath(QStringLiteral("b.md")));
        QAction* splitRight = actionNamed(window, QStringLiteral("Split Right"));
        QVERIFY(splitRight);
        QCOMPARE(splitRight->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Backslash));
        splitRight->trigger();
        Workspace* workspace = window.tabs();
        QCOMPARE(workspace->panes().size(), 2);
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("b.md"))); // moved right, still current

        // A new note opens in the current pane.
        window.openNote(vault.filePath(QStringLiteral("c.md")));
        QCOMPARE(workspace->paneOf(window.currentView()), workspace->panes()[1]);
        // A note already open elsewhere is shown where it is, never opened twice.
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        QCOMPARE(workspace->currentPane(), workspace->panes()[0]);
        QCOMPARE(workspace->count(), 3);
        // Focus Next Pane, and the sidebar follows the pane in focus.
        actionNamed(window, QStringLiteral("Focus Next Pane"))->trigger();
        QCOMPARE(workspace->currentPane(), workspace->panes()[1]);
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("c.md")));
        // Closing the right pane's tabs leaves one pane.
        while (workspace->panes().size() > 1 && workspace->panes()[1]->count() > 0)
            emit workspace->panes()[1]->tabCloseRequested(0); // its close button
        QCOMPARE(workspace->panes().size(), 1);
        QCOMPARE(workspace->count(), 1);
        QVERIFY(window.currentView());
    }

    // F-012 step 2: a note moved to a window of its own, which shares the
    // vault's session: one index, one note in one place, shared bookmarks.
    void notesMoveToWindowsSharingTheSession()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "# A\n\nSee [[b]].\n");
        write(vault.filePath(QStringLiteral("b.md")), "# B\n\nBody.\n");
        const QString a = vault.filePath(QStringLiteral("a.md"));
        const QString b = vault.filePath(QStringLiteral("b.md"));
        std::weak_ptr<VaultSession> session;
        {
            MainWindow window;
            window.resize(1000, 700);
            window.show();
            QSignalSpy opened(&window, &MainWindow::indexOpened);
            window.openVault(*Vault::open(vault.path()));
            QVERIFY(opened.wait(5000));
            session = window.session();
            window.openNote(a);
            window.openNote(b);
            Editor* editor = window.currentEditor();
            editor->moveCursor(QTextCursor::End);
            editor->insertPlainText(QStringLiteral("Unsaved.\n"));
            const int cursor = editor->textCursor().position();

            QAction* move = actionNamed(window, QStringLiteral("Move to New Window"));
            QVERIFY(move);
            QCOMPARE(move->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W));
            QPointer<MainWindow> second = window.moveToNewWindow();
            QVERIFY(second);
            QVERIFY(second->testAttribute(Qt::WA_DeleteOnClose));
            // The note left this window, with its unsaved text and cursor.
            QCOMPARE(window.tabs()->count(), 1);
            QVERIFY(!window.editorFor(b));
            Editor* moved = second->currentEditor();
            QCOMPARE(moved->path(), b);
            QVERIFY(moved->isModified());
            QVERIFY(moved->toPlainText().endsWith(QStringLiteral("Unsaved.\n")));
            QCOMPARE(moved->textCursor().position(), cursor);
            // One session, already indexed: the window joining it says so at once.
            QCOMPARE(second->session(), window.session());
            QCOMPARE(window.session()->windows().size(), 2);
            QVERIFY(actionNamed(*second, QStringLiteral("Verify Index"))->isEnabled()); // without waiting
            QCOMPARE(second->vault()->rootPath(), vault.path());

            // A note is open in one place: asking either window for it shows it where it is.
            QCOMPARE(window.tabs()->count(), 1);
            window.openNote(b);
            QCOMPARE(window.tabs()->count(), 1);
            QCOMPARE(second->tabs()->count(), 1);
            second->openNote(a);
            QCOMPARE(second->tabs()->count(), 1);

            // Bookmarks are the session's: starred in one window, shown in both.
            second->toggleBookmark(a);
            QVERIFY(window.isBookmarked(a));
            QVERIFY(window.bookmarks() == second->bookmarks());

            // Closing the second window asks about its unsaved note like any
            // other; saved, it closes and its notes are free again.
            QVERIFY(second->saveCurrent());
            second->close();
            QTRY_VERIFY(second.isNull());
            QCOMPARE(window.session()->windows().size(), 1);
            QVERIFY(QSettings().contains(QStringLiteral("secondaryWindow/geometry"))); // its own layout
            window.openNote(b);
            QCOMPARE(window.tabs()->count(), 2);

            // The first window closing while another has the vault keeps the session.
            QPointer<MainWindow> third = window.moveToNewWindow();
            QVERIFY(third);
            window.close();
            QVERIFY(!session.expired());
            QVERIFY(!window.session());
            QVERIFY(third->session()->index());
            actionNamed(*third, QStringLiteral("Quit"))->trigger(); // every window
            QTRY_VERIFY(third.isNull());
        }
        QVERIFY(session.expired()); // the last window took the workers with it
    }
};

QTEST_MAIN(TestWorkspace)
#include "test_workspace.moc"
