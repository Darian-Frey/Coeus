#include "ui/CommandPalette.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/MainWindow.hpp"
#include "vault/Vault.hpp"

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

using coeus::MainWindow;
using coeus::Vault;

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void init()
    {
        QSettings().clear();
    }

    void hasShellComponents()
    {
        MainWindow window;
        QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("explorerDock")));
        QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("sidebarDock")));
        QVERIFY(window.tabs());
        QVERIFY(window.statusBar());
        QVERIFY(!window.registeredActions().isEmpty());
    }

    void opensNoteReadOnlyOnce()
    {
        QTemporaryDir dir;
        const QString path = writeNote(dir, QStringLiteral("alpha.md"), "# Alpha\n");

        MainWindow window;
        window.openVault(*Vault::open(dir.path()));
        window.openNote(path);
        window.openNote(path);

        QCOMPARE(window.tabs()->count(), 1);
        auto* editor = qobject_cast<QPlainTextEdit*>(window.tabs()->currentWidget());
        QVERIFY(editor);
        QVERIFY(editor->isReadOnly());
        QCOMPARE(editor->toPlainText(), QStringLiteral("# Alpha\n"));
        QCOMPARE(window.tabs()->tabText(0), QStringLiteral("alpha"));
    }

    void ignoresAttachments()
    {
        QTemporaryDir dir;
        const QString path = writeNote(dir, QStringLiteral("image.png"), "png");

        MainWindow window;
        window.openVault(*Vault::open(dir.path()));
        window.openNote(path);
        QCOMPARE(window.tabs()->count(), 0);
    }

    void dockLayoutPersists()
    {
        {
            MainWindow window;
            window.findChild<QDockWidget*>(QStringLiteral("sidebarDock"))->hide();
            window.close();
        }
        MainWindow restored;
        QVERIFY(restored.findChild<QDockWidget*>(QStringLiteral("sidebarDock"))->isHidden());
        QVERIFY(!restored.findChild<QDockWidget*>(QStringLiteral("explorerDock"))->isHidden());
    }

    void paletteFiltersAndTriggers()
    {
        MainWindow window;
        auto* palette = window.commandPalette();
        const int all = int(window.registeredActions().size());

        palette->setFilterText(QString());
        QCOMPARE(palette->visibleCount(), all);

        palette->setFilterText(QStringLiteral("tglsid"));
        QCOMPARE(palette->visibleCount(), 1);

        palette->setFilterText(QStringLiteral("zzzz"));
        QCOMPARE(palette->visibleCount(), 0);

        auto* custom = new QAction(QStringLiteral("Frobnicate Registers"), &window);
        QSignalSpy triggered(custom, &QAction::triggered);
        window.registerAction(custom);
        palette->popup();
        palette->setFilterText(QStringLiteral("frob"));
        QCOMPARE(palette->visibleCount(), 1);
        QTest::keyClick(palette->focusWidget(), Qt::Key_Return);
        QCOMPARE(triggered.count(), 1);
    }

private:
    static QString writeNote(const QTemporaryDir& dir, const QString& name, const QByteArray& content)
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(content);
        return path;
    }

    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
