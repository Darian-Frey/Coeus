// F-021 in the window: diffs against HEAD and between versions, and the
// blame gutter, over a scratch repository.

#include "../git/GitFixture.hpp"

#include "ui/DiffView.hpp"
#include "ui/Editor.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"

#include <QAction>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTreeWidget>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr int kWaitMs = 5000;
constexpr qint64 kStart = 1'790'000'000;

QAction* menuAction(QMenu* menu, const QString& text)
{
    for (QAction* action : menu->actions()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

} // namespace

class TestDiffBlame : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_dir;
    std::unique_ptr<GitFixture> m_git;
    std::unique_ptr<MainWindow> m_window;
    QString m_first;
    QString m_second;

    QString note(const char* name) const { return m_dir.filePath(QStringLiteral("repo/")) + QLatin1String(name); }

    void showHistory(const QString& path)
    {
        QSignalSpy updated(m_window->history(), &HistoryPanel::updated);
        const bool shown = m_window->history()->notePath() == path;
        m_window->openNote(path);
        if (!shown) // the note already shown is not read again
            QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty() && m_window->history()->notePath() == path, kWaitMs);
    }

    DiffView* waitForDiff(QSignalSpy& opened)
    {
        if (opened.isEmpty() && !opened.wait(kWaitMs))
            return nullptr;
        return opened.takeFirst().at(0).value<DiffView*>();
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<coeus::DiffView*>();
        qRegisterMetaType<coeus::Editor*>();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        m_git = std::make_unique<GitFixture>(m_dir.filePath(QStringLiteral("repo")));
        m_git->write(QStringLiteral("plan.md"), "# Plan\nalpha\nbeta\ngamma\n");
        m_first = m_git->commit(QStringLiteral("Draft"), {QStringLiteral("plan.md")}, kStart);
        m_git->write(QStringLiteral("plan.md"), "# Plan\nalpha\nBETA\ngamma\n");
        m_second = m_git->commit(QStringLiteral("Shout beta"), {QStringLiteral("plan.md")}, kStart + 86400);
        m_git->write(QStringLiteral("other.md"), "Other.\n");
        m_git->commit(QStringLiteral("Other"), {QStringLiteral("other.md")}, kStart + 2 * 86400);
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1100, 750);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_dir.filePath(QStringLiteral("repo"))));
        QVERIFY(reloaded.wait(kWaitMs));
    }

    void diffWithHeadIncludesUnsavedChanges()
    {
        showHistory(note("plan.md"));
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("delta\n")); // unsaved
        QSignalSpy opened(m_window.get(), &MainWindow::diffOpened);
        m_window->history()->diffButton()->click();
        DiffView* diff = waitForDiff(opened);
        QVERIFY(diff);
        QCOMPARE(diff->added(), 1);
        QCOMPARE(diff->removed(), 0);
        QVERIFY(diff->summary().contains(QStringLiteral("HEAD → working copy")));
        QCOMPARE(diff->text()->document()->lastBlock().text(), QStringLiteral("        5 + delta"));
        QCOMPARE(m_window->tabs()->currentWidget(), diff);
        QCOMPARE(m_window->history()->notePath(), note("plan.md")); // the diff keeps its note's history in view

        // Again, after another edit: the same tab, compared afresh.
        const int tabs = m_window->tabs()->count();
        editor->insertPlainText(QStringLiteral("epsilon\n"));
        m_window->diffWithHead(note("plan.md"));
        DiffView* again = waitForDiff(opened);
        QCOMPARE(again->added(), 2);
        QCOMPARE(m_window->tabs()->count(), tabs);
        editor->document()->setModified(false);
    }

    void compareVersionsFromTheHistory()
    {
        showHistory(note("plan.md"));
        HistoryPanel* history = m_window->history();
        QTreeWidget* tree = history->tree();
        QCOMPARE(tree->topLevelItemCount(), 2);
        QSignalSpy opened(m_window.get(), &MainWindow::diffOpened);

        // The newer commit against the version before it.
        tree->clearSelection();
        tree->topLevelItem(0)->setSelected(true);
        menuAction(history->commitMenu(), QStringLiteral("Compare with the Version Before"))->trigger();
        DiffView* step = waitForDiff(opened);
        QVERIFY(step);
        QCOMPARE(step->removed(), 1);
        QCOMPARE(step->added(), 1);
        QCOMPARE(step->title(), QStringLiteral("plan: %1 ↔ %2").arg(m_first.left(7), m_second.left(7)));
        QVERIFY(step->nextChange());
        QCOMPARE(step->text()->textCursor().block().text(), QStringLiteral("   3      − beta"));

        // The first commit has nothing before it: everything was added.
        tree->clearSelection();
        tree->topLevelItem(1)->setSelected(true);
        menuAction(history->commitMenu(), QStringLiteral("Compare with the Version Before"))->trigger();
        DiffView* first = waitForDiff(opened);
        QVERIFY(first);
        QCOMPARE(first->added(), 4);
        QCOMPARE(first->removed(), 0);

        // Two selected: older against newer, whichever was selected first.
        tree->clearSelection();
        tree->topLevelItem(1)->setSelected(true);
        tree->topLevelItem(0)->setSelected(true);
        QMenu* menu = history->commitMenu();
        QCOMPARE(menu->actions().size(), 1);
        const int tabs = m_window->tabs()->count();
        menu->actions().front()->trigger();
        QTest::qWait(100); // the same two commits: the open tab, no new one
        QCOMPARE(m_window->tabs()->count(), tabs);
        QCOMPARE(m_window->tabs()->currentWidget(), step);
    }

    void blameGutterNamesCommitsAndFollowsEdits()
    {
        showHistory(note("plan.md"));
        Editor* editor = m_window->currentEditor();
        QSignalSpy applied(m_window.get(), &MainWindow::blameApplied);
        m_window->history()->blameButton()->click();
        QVERIFY(editor->blameShown());
        QVERIFY(applied.wait(kWaitMs));
        // "# Plan" from the first commit opens a run; "alpha" continues it; "BETA" is the second's.
        QCOMPARE(editor->blameLabel(0), QStringLiteral("%1 2026-09-21").arg(m_first.left(7)));
        QCOMPARE(editor->blameLabel(1), QString());
        QCOMPARE(editor->blameLabel(2), QStringLiteral("%1 2026-09-22").arg(m_second.left(7)));
        QVERIFY(editor->blameToolTip(2).contains(QStringLiteral("Shout beta")));
        QCOMPARE(editor->blameLabel(3), QStringLiteral("%1 2026-09-21").arg(m_first.left(7)));
        QVERIFY(editor->viewport()->x() >= editor->blameGutterWidth()); // the text moved right for the gutter

        // Typing a line: once typing pauses, blamed again, and the new line is not committed.
        editor->moveCursor(QTextCursor::Start);
        editor->insertPlainText(QStringLiteral("New first line\n"));
        QVERIFY(applied.wait(kWaitMs));
        QCOMPARE(editor->blameLabel(0), QStringLiteral("Not committed"));
        QCOMPARE(editor->blameLabel(1), QStringLiteral("%1 2026-09-21").arg(m_first.left(7)));
        editor->document()->undo();
        editor->document()->setModified(false);

        // Another note: its own state, so the button follows the tab.
        m_window->openNote(note("other.md"));
        QVERIFY(!m_window->history()->blameButton()->isChecked());
        m_window->openNote(note("plan.md"));
        QVERIFY(m_window->history()->blameButton()->isChecked());

        // Off from the keyboard action.
        QAction* toggle = nullptr;
        for (QAction* action : m_window->registeredActions()) {
            if (action->text().remove(QLatin1Char('&')) == QLatin1String("Toggle Blame"))
                toggle = action;
        }
        QVERIFY(toggle);
        toggle->trigger();
        QVERIFY(!editor->blameShown());
        QVERIFY(editor->viewport()->x() < editor->blameGutterWidth());
        QVERIFY(!m_window->history()->blameButton()->isChecked());
    }

    void cleanupTestCase()
    {
        for (int i = 0; i < m_window->tabs()->count(); ++i) {
            if (Editor* editor = m_window->editorAt(i))
                editor->document()->setModified(false);
        }
        m_window.reset();
        m_git.reset();
    }
};

QTEST_MAIN(TestDiffBlame)
#include "test_diff_blame.moc"
