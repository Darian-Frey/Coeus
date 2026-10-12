#include "ui/CommandPalette.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/MainWindow.hpp"
#include "vault/GitIgnoreOffer.hpp"
#include "vault/Roots.hpp"
#include "vault/Vault.hpp"

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QFileSystemModel>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QTreeView>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

#include <git2.h>

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

    void openingVaultSweepsStaleTempFiles()
    {
        QTemporaryDir dir;
        const QString stale = writeNote(dir, QStringLiteral(".alpha.md.coeus-tmp-abc123"), "partial");
        QFile staleFile(stale);
        QVERIFY(staleFile.open(QIODevice::ReadWrite)); // setFileTime needs an open file
        QVERIFY(staleFile.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-600),
                                      QFileDevice::FileModificationTime));
        staleFile.close();

        MainWindow window;
        QSignalSpy finished(&window, &MainWindow::tempSweepFinished);
        window.openVault(*Vault::open(dir.path()));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.front().front().toInt(), 1);
        QVERIFY(!QFile::exists(stale));
    }

    void explorerShowsLinkedRootsAndOpensTheirNotes()
    {
        // F-034: the vault and each linked root are top-level sections; a
        // missing root is greyed, not dropped.
        QTemporaryDir vault, repo;
        writeNote(vault, QStringLiteral("home.md"), "home");
        writeNote(repo, QStringLiteral("README.md"), "# Repo\n");
        QDir(vault.path()).mkdir(QStringLiteral(".coeus"));
        QFile config(coeus::Roots::configPath(vault.path()));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(QStringLiteral("[[roots]]\nname = \"repo\"\npath = \"%1\"\n"
                                    "[[roots]]\nname = \"gone\"\npath = \"/nonexistent/coeus-ui-test\"\n")
                         .arg(repo.path())
                         .toUtf8());
        config.close();

        MainWindow window;
        window.openVault(*Vault::open(vault.path()));
        coeus::ExplorerPanel* explorer = window.explorer();
        QCOMPARE(explorer->sectionCount(), 3);
        QCOMPARE(explorer->sectionHeader(1)->text(), QStringLiteral("repo"));
        QVERIFY(explorer->sectionHeader(1)->isEnabled());
        QVERIFY(explorer->sectionHeader(2)->text().startsWith(QStringLiteral("gone")));
        QVERIFY(!explorer->sectionHeader(2)->isEnabled());
        QVERIFY(!explorer->view(2));
        QCOMPARE(explorer->model(1)->rootPath(), QFileInfo(repo.path()).canonicalFilePath());

        // Collapsing a section hides its tree.
        explorer->sectionHeader(1)->setChecked(false);
        QVERIFY(explorer->view(1)->isHidden());

        // A linked root's note opens, labelled "<root>/<path>".
        window.openNote(repo.filePath(QStringLiteral("README.md")));
        QCOMPARE(window.tabs()->count(), 1);
        QCOMPARE(window.tabs()->tabToolTip(0), QStringLiteral("repo/README.md"));
        QCOMPARE(qobject_cast<QPlainTextEdit*>(window.tabs()->widget(0))->toPlainText(), QStringLiteral("# Repo\n"));

        // Anything outside every root is refused.
        QTemporaryDir outside;
        window.openNote(writeNote(outside, QStringLiteral("x.md"), "x"));
        QCOMPARE(window.tabs()->count(), 1);
    }

    void gitignoreOfferAddsLinesOnce()
    {
        QTemporaryDir vault;
        QVERIFY(initRepository(vault.path()));
        {
            MainWindow window;
            QSignalSpy offered(&window, &MainWindow::gitignoreOffered);
            QSignalSpy updated(&window, &MainWindow::gitignoreUpdated);
            window.openVault(*Vault::open(vault.path()));
            QVERIFY(offered.wait(5000));
            auto* box = window.findChild<QMessageBox*>(QStringLiteral("gitignoreOffer"));
            QVERIFY(box);
            QVERIFY(box->isVisible());
            QFile gitignore(vault.filePath(QStringLiteral(".gitignore")));
            QVERIFY(!gitignore.exists()); // nothing written before the author answers

            box->findChild<QPushButton*>(QStringLiteral("add"))->click();
            QVERIFY(updated.wait(5000));
            QVERIFY(gitignore.open(QIODevice::ReadOnly));
            QVERIFY(gitignore.readAll().contains(".coeus/\n"));
        }
        // Covered now: no second offer.
        MainWindow again;
        QSignalSpy offered(&again, &MainWindow::gitignoreOffered);
        again.openVault(*Vault::open(vault.path()));
        QVERIFY(!offered.wait(1000));
    }

    void gitignoreOfferDeclinedIsNotRepeated()
    {
        QTemporaryDir vault;
        QVERIFY(initRepository(vault.path()));
        {
            MainWindow window;
            QSignalSpy offered(&window, &MainWindow::gitignoreOffered);
            window.openVault(*Vault::open(vault.path()));
            QVERIFY(offered.wait(5000));
            window.findChild<QMessageBox*>(QStringLiteral("gitignoreOffer"))
                ->findChild<QPushButton*>(QStringLiteral("never"))
                ->click();
            QVERIFY(coeus::GitIgnoreOffer::declined(vault.path()));
            QVERIFY(!QFile::exists(vault.filePath(QStringLiteral(".gitignore"))));
        }
        MainWindow again;
        QSignalSpy offered(&again, &MainWindow::gitignoreOffered);
        again.openVault(*Vault::open(vault.path()));
        QVERIFY(!offered.wait(1000));
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
        // Disabled commands (Verify and Rebuild Index without a vault) are hidden.
        const int all = int(std::count_if(window.registeredActions().begin(), window.registeredActions().end(),
                                          [](const QAction* action) { return action->isEnabled(); }));
        QVERIFY(all < window.registeredActions().size());

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
    static bool initRepository(const QString& path)
    {
        git_libgit2_init();
        git_repository* repo = nullptr;
        const int rc = git_repository_init(&repo, QFile::encodeName(path).constData(), 0);
        git_repository_free(repo);
        git_libgit2_shutdown();
        return rc == 0;
    }

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
