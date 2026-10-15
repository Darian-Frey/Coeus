#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteView.hpp"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
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

} // namespace

class TestNoteView : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void switchesModesAndRenders()
    {
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "# Heading\n\nSome **bold** text.\n");
        NoteView view(*Vault::open(vault.path()), path, QStringLiteral("n.md"));
        QVERIFY(view.editor()->load().has_value());
        QCOMPARE(view.mode(), NoteView::Mode::Source);

        QSignalSpy changed(&view, &NoteView::modeChanged);
        view.toggleMode();
        QCOMPARE(view.mode(), NoteView::Mode::Reading);
        QCOMPARE(changed.size(), 1);
        QVERIFY(view.reader()->toPlainText().contains(QStringLiteral("Some bold text.")));
        QVERIFY(!view.reader()->toPlainText().contains(QStringLiteral("**")));
        QVERIFY(!view.editor()->isModified()); // reading never changes the buffer

        view.toggleMode();
        QCOMPARE(view.mode(), NoteView::Mode::Source);
    }

    void readingViewFollowsTheBuffer()
    {
        QTemporaryDir vault;
        const QString path = vault.filePath(QStringLiteral("n.md"));
        write(path, "before\n");
        NoteView view(*Vault::open(vault.path()), path, QStringLiteral("n.md"));
        QVERIFY(view.editor()->load().has_value());
        view.setMode(NoteView::Mode::Reading);
        view.editor()->selectAll();
        view.editor()->insertPlainText(QStringLiteral("after\n"));
        QTRY_VERIFY_WITH_TIMEOUT(view.reader()->toPlainText().contains(QStringLiteral("after")), 2000);
    }

    void imagesResolveInsideTheRootOnly()
    {
        QTemporaryDir vault, outside;
        write(vault.filePath(QStringLiteral("img/pic.png")), "png");
        write(vault.filePath(QStringLiteral("notes/n.md")), "![a](../img/pic.png) ![[img/pic.png]] ![b](../../escape.png)\n");
        write(outside.filePath(QStringLiteral("escape.png")), "png");
        NoteView view(*Vault::open(vault.path()), vault.filePath(QStringLiteral("notes/n.md")), QStringLiteral("notes/n.md"));
        QVERIFY(view.editor()->load().has_value());
        view.setMode(NoteView::Mode::Reading);
        const QString html = view.reader()->toHtml();
        QCOMPARE(html.count(QStringLiteral("src=\"file://")), 2); // both forms load the image
        QVERIFY(html.contains(QStringLiteral("[../../escape.png]"))); // not loaded from outside the vault
    }

    void readingModeLinksOpenNotes()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "[[b]] [down](sub/c.md)\n");
        write(vault.filePath(QStringLiteral("b.md")), "# B\n");
        write(vault.filePath(QStringLiteral("sub/c.md")), "# C\n");
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(5000));
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        NoteView* view = window.currentView();
        view->setMode(NoteView::Mode::Reading);

        emit view->reader()->anchorClicked(QUrl(QStringLiteral("coeus-wiki:b")));
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("b.md")));

        window.tabs()->setCurrentWidget(view);
        emit view->reader()->anchorClicked(QUrl(QStringLiteral("coeus-file:sub/c.md")));
        QCOMPARE(window.currentEditor()->path(), vault.filePath(QStringLiteral("sub/c.md")));

        window.tabs()->setCurrentWidget(view);
        emit view->reader()->anchorClicked(QUrl(QStringLiteral("coeus-wiki:nowhere")));
        QCOMPARE(window.currentView(), view); // unresolved: stays, says so
        QCOMPARE(window.tabs()->count(), 3);
    }

    void embedsResolveThroughTheIndexAndFollowChanges()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("host.md")), "# Host\n\n![[guide#Install]]\n");
        write(vault.filePath(QStringLiteral("docs/guide.md")), "# Guide\n\n## Install\n\nRun the installer.\n\n## Use\n\nLater.\n");
        MainWindow window;
        QSignalSpy opened(&window, &MainWindow::indexOpened);
        window.openVault(*Vault::open(vault.path()));
        QVERIFY(opened.wait(5000));
        window.openNote(vault.filePath(QStringLiteral("host.md")));
        NoteView* view = window.currentView();
        view->setMode(NoteView::Mode::Reading);
        QString text = view->reader()->toPlainText();
        QVERIFY2(text.contains(QStringLiteral("Run the installer.")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("guide › Install")));
        QVERIFY(!text.contains(QStringLiteral("Later.")));

        // The embedded note changes on disk: the reading view follows once it is indexed.
        write(vault.filePath(QStringLiteral("docs/guide.md")), "# Guide\n\n## Install\n\nRun the new installer.\n");
        QTRY_VERIFY_WITH_TIMEOUT(view->reader()->toPlainText().contains(QStringLiteral("Run the new installer.")), 5000);
    }

    void ctrlETogglesTheCurrentTab()
    {
        QTemporaryDir vault;
        write(vault.filePath(QStringLiteral("a.md")), "x\n");
        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        window.openNote(vault.filePath(QStringLiteral("a.md")));
        bool found = false;
        for (QAction* action : window.registeredActions()) {
            if (action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E)) {
                action->trigger();
                found = true;
            }
        }
        QVERIFY(found);
        QCOMPARE(window.currentView()->mode(), NoteView::Mode::Reading);
    }
};

QTEST_MAIN(TestNoteView)
#include "test_note_view.moc"
