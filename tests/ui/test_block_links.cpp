// F-024 in the window: Copy Block Link names the paragraph at the cursor,
// giving it an ID first; heading and block links open at their line.

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteView.hpp"

#include <QAction>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>

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

} // namespace

class TestBlockLinks : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void copyBlockLinkGivesAnIdOnce()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("notes/plan.md")), "# Plan\n\nShip it\nsoon.\n\n- a task ^task\n");
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(5000));
        window.openNote(vault.filePath(QStringLiteral("notes/plan.md")));
        Editor* editor = window.currentEditor();
        QAction* copy = actionNamed(window, QStringLiteral("Copy Block Link"));
        QVERIFY(copy);
        QCOMPARE(copy->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L));

        // A block with an ID: copied as it is, the note untouched.
        QTextCursor cursor(editor->document()->findBlockByNumber(5)); // "- a task ^task"
        editor->setTextCursor(cursor);
        copy->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("[[plan#^task]]"));
        QVERIFY(!editor->isModified());

        // A paragraph without one: an ID at the end of its last line, one undo step.
        editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(2))); // "Ship it"
        copy->trigger();
        const QString link = QGuiApplication::clipboard()->text();
        QVERIFY2(link.startsWith(QStringLiteral("[[plan#^")) && link.size() == QStringLiteral("[[plan#^abc123]]").size(), qPrintable(link));
        const QString id = link.mid(8, 6);
        QCOMPARE(editor->document()->findBlockByNumber(3).text(), QStringLiteral("soon. ^") + id);
        QVERIFY(editor->isModified());
        copy->trigger(); // again: the same ID, nothing added
        QCOMPARE(QGuiApplication::clipboard()->text(), link);
        QCOMPARE(editor->toPlainText().count(QStringLiteral("^") + id), 1);
        editor->undo();
        QCOMPARE(editor->document()->findBlockByNumber(3).text(), QStringLiteral("soon."));

        // A heading or a blank line names no block.
        editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
        QGuiApplication::clipboard()->setText(QStringLiteral("unchanged"));
        copy->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("unchanged"));
    }

    void headingAndBlockLinksOpenAtTheirLine()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "[[b#Second part]] [[b#^tail]] [[b^tail]] [[b#^gone]]\n");
        write(vault.filePath(QStringLiteral("b.md")), "# B\n\nOne.\n\n## Second part\n\nTwo.\n\nThe end. ^tail\n");
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(5000));
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        NoteView* view = window.currentView();
        view->setMode(NoteView::Mode::Reading);
        const auto follow = [&](const char* url) {
            window.tabs()->setCurrentWidget(view);
            emit view->reader()->anchorClicked(QUrl(QString::fromLatin1(url)));
            return window.currentEditor()->textCursor().blockNumber() + 1;
        };
        QCOMPARE(follow("coeus-wiki:b#Second%20part"), 5);
        QCOMPARE(follow("coeus-wiki:b#%5Etail"), 9);
        QCOMPARE(follow("coeus-wiki:b%5Etail"), 9);
        // No such block: the note opens, its cursor where it was, and says so.
        QCOMPARE(follow("coeus-wiki:b#%5Egone"), 9);
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("b.md")));
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("has no block ^gone")));
    }
};

QTEST_MAIN(TestBlockLinks)
#include "test_block_links.moc"
