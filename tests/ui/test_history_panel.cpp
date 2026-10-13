// F-020 in the window: the History section and read-only versions.

#include "../git/GitFixture.hpp"

#include "ui/Editor.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/VersionView.hpp"

#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr int kWaitMs = 5000;
constexpr qint64 kStart = 1'790'000'000;

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestHistoryPanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_dir;
    std::unique_ptr<GitFixture> m_git;
    std::unique_ptr<MainWindow> m_window;
    QString m_first;

    QString vault(const char* relative) const { return m_dir.filePath(QStringLiteral("repo/vault/")) + QLatin1String(relative); }

    // Shows `note` and waits for its history.
    void showHistory(const QString& note)
    {
        QSignalSpy updated(m_window->history(), &HistoryPanel::updated);
        m_window->openNote(note);
        QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty() && m_window->history()->notePath() == note, kWaitMs);
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        // The vault is a subfolder of the repository (F-020: "or a subfolder").
        m_git = std::make_unique<GitFixture>(m_dir.filePath(QStringLiteral("repo")));
        m_git->write(QStringLiteral("vault/plan.md"), "# Plan\n\nFirst draft.\n");
        m_first = m_git->commit(QStringLiteral("Draft the plan"), {QStringLiteral("vault/plan.md")}, kStart);
        m_git->write(QStringLiteral("vault/plan.md"), "# Plan\n\nSecond draft.\n");
        m_git->commit(QStringLiteral("Revise the plan\n\nMore detail."), {QStringLiteral("vault/plan.md")}, kStart + 86400);
        m_git->write(QStringLiteral("vault/plan.md"), "# Plan\n\nWorking copy.\n");
        m_git->write(QStringLiteral("vault/new.md"), "Never committed.\n");
        QDir().mkpath(m_dir.filePath(QStringLiteral("elsewhere")));
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1100, 750);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_dir.filePath(QStringLiteral("repo/vault"))));
        QVERIFY(reloaded.wait(kWaitMs));
    }

    void showsTheNotesCommits()
    {
        showHistory(vault("plan.md"));
        HistoryPanel* history = m_window->history();
        QCOMPARE(history->branch(), QStringLiteral("main"));
        QCOMPARE(history->status(), QStringLiteral("2 commits"));
        QTreeWidget* tree = history->tree();
        QCOMPARE(tree->topLevelItemCount(), 2);
        QVERIFY(tree->topLevelItem(0)->text(1).startsWith(QStringLiteral("Revise the plan\n")));
        QVERIFY(tree->topLevelItem(0)->text(1).endsWith(QStringLiteral(" · Ada Lovelace")));
        QCOMPARE(tree->topLevelItem(1)->text(0), m_first.left(7));
    }

    void opensAVersionReadOnly()
    {
        QTreeWidget* tree = m_window->history()->tree();
        QSignalSpy opened(m_window.get(), &MainWindow::versionOpened);
        emit tree->itemClicked(tree->topLevelItem(1), 0);
        QVERIFY(opened.wait(kWaitMs));
        auto* view = qobject_cast<VersionView*>(m_window->tabs()->currentWidget());
        QVERIFY(view);
        QCOMPARE(view->text()->toPlainText(), QStringLiteral("# Plan\n\nFirst draft.\n"));
        QVERIFY(view->text()->isReadOnly());
        QCOMPARE(m_window->tabs()->tabText(m_window->tabs()->currentIndex()), QStringLiteral("plan @ %1").arg(m_first.left(7)));
        QVERIFY(!m_window->currentEditor()); // no editor, so nothing can save it
        QVERIFY(!m_window->saveCurrent());
        QCOMPARE(read(vault("plan.md")), QByteArray("# Plan\n\nWorking copy.\n"));
        // The section keeps showing the note's history while its version is open.
        QCOMPARE(m_window->history()->notePath(), vault("plan.md"));

        // The same version again: the open tab, not a second one.
        const int tabs = m_window->tabs()->count();
        emit tree->itemClicked(tree->topLevelItem(1), 0);
        QCOMPARE(m_window->tabs()->count(), tabs);
        QCOMPARE(m_window->tabs()->currentWidget(), view);

        // Opening the note itself goes to its editor, not the version.
        m_window->openNote(vault("plan.md"));
        QVERIFY(m_window->currentEditor());
        QCOMPARE(m_window->currentEditor()->toPlainText(), QStringLiteral("# Plan\n\nWorking copy.\n"));

        // And the version's tab closes like any other.
        emit m_window->tabs()->tabCloseRequested(m_window->tabs()->indexOf(view));
        QCOMPARE(m_window->tabs()->count(), tabs - 1);
    }

    void untrackedAndOutsideARepository()
    {
        showHistory(vault("new.md"));
        QCOMPARE(m_window->history()->status(), QStringLiteral("No commits of this note yet."));
        QVERIFY(!m_window->history()->tree()->isVisible());

        QSignalSpy updated(m_window->history(), &HistoryPanel::updated);
        m_window->history()->showNote(m_dir.filePath(QStringLiteral("elsewhere/loose.md")));
        QVERIFY(updated.wait(kWaitMs));
        QCOMPARE(m_window->history()->status(), QStringLiteral("Not in a Git repository"));
        QCOMPARE(m_window->history()->branch(), QString());
    }

    void newCommitsShowOnReturningToTheWindow()
    {
        showHistory(vault("plan.md"));
        m_git->commit(QStringLiteral("Commit the working copy"), {QStringLiteral("vault/plan.md")}, kStart + 2 * 86400);
        QSignalSpy updated(m_window->history(), &HistoryPanel::updated);
        QEvent activation(QEvent::ActivationChange);
        m_window->activateWindow();
        QApplication::sendEvent(m_window.get(), &activation);
        if (!m_window->isActiveWindow())
            m_window->history()->refresh(); // offscreen platforms may not activate windows
        QVERIFY(updated.wait(kWaitMs));
        QCOMPARE(m_window->history()->status(), QStringLiteral("3 commits"));
    }

    void cleanupTestCase()
    {
        m_window.reset();
        m_git.reset();
    }
};

QTEST_MAIN(TestHistoryPanel)
#include "test_history_panel.moc"
