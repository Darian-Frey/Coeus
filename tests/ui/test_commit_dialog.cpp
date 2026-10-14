// F-022 in the window: the changed-files count, the commit dialog, and push.

#include "../git/GitFixture.hpp"

#include "git/GitRepository.hpp"
#include "ui/CommitDialog.hpp"
#include "ui/Editor.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr int kWaitMs = 10'000;
constexpr qint64 kStart = 1'790'000'000;

QTreeWidgetItem* row(QTreeWidget* tree, const QString& path)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(1) == path)
            return tree->topLevelItem(i);
    }
    return nullptr;
}

} // namespace

class TestCommitDialog : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_dir;
    std::unique_ptr<GitFixture> m_git;
    std::unique_ptr<MainWindow> m_window;

    QString repo(const char* relative = "") const { return m_dir.filePath(QStringLiteral("repo/")) + QLatin1String(relative); }
    QString remote() const { return m_dir.filePath(QStringLiteral("remote.git")); }

    // Opens the dialog and waits until it shows the repository's changes.
    CommitDialog* openDialog()
    {
        QSignalSpy ready(m_window.get(), &MainWindow::commitDialogReady);
        m_window->openCommitDialog();
        return ready.wait(kWaitMs) ? m_window->commitDialog() : nullptr;
    }

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is not installed");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QVERIFY(GitFixture::initBare(remote()));
        m_git = std::make_unique<GitFixture>(repo());
        m_git->setIdentity();
        m_git->setUpstream(remote());
        m_git->write(QStringLiteral(".gitignore"), ".coeus/\n");
        m_git->write(QStringLiteral("plan.md"), "# Plan\n");
        m_git->write(QStringLiteral("ideas.md"), "# Ideas\n");
        m_git->commit(QStringLiteral("Start"), {QStringLiteral(".gitignore"), QStringLiteral("plan.md"), QStringLiteral("ideas.md")}, kStart);
        m_git->write(QStringLiteral("ideas.md"), "# Ideas\n\nChanged outside Coeus.\n");
        m_git->write(QStringLiteral("journal.md"), "New note.\n");
        m_git->write(QStringLiteral("scratch.txt"), "Not a note.\n");

        m_window = std::make_unique<MainWindow>();
        m_window->resize(1100, 750);
        m_window->show();
        QSignalSpy status(m_window.get(), &MainWindow::gitStatusUpdated);
        m_window->openVault(*Vault::open(repo()));
        QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty() && status.last().at(0).toInt() == 3, kWaitMs);
    }

    void theStatusBarCountsChangedFiles()
    {
        QToolButton* button = m_window->gitButton();
        QVERIFY(button->isVisible());
        QCOMPARE(button->text(), QStringLiteral("● 3 file(s) changed"));
    }

    void unsavedNotesAreFlaggedAndCanBeSaved()
    {
        m_window->openNote(repo("plan.md"));
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("\nTyped, not saved.\n"));
        CommitDialog* dialog = openDialog();
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->saveAllButton()->isVisible());
        QVERIFY(!row(dialog->tree(), QStringLiteral("plan.md"))); // on disk it is unchanged

        QSignalSpy ready(m_window.get(), &MainWindow::commitDialogReady);
        dialog->saveAllButton()->click();
        QVERIFY(ready.wait(kWaitMs));
        QVERIFY(!editor->isModified());
        QVERIFY(!dialog->saveAllButton()->isVisible());
        QVERIFY(row(dialog->tree(), QStringLiteral("plan.md")));
    }

    void ticksAndMessageGateTheCommit()
    {
        CommitDialog* dialog = m_window->commitDialog();
        QTreeWidget* tree = dialog->tree();
        QCOMPARE(tree->topLevelItemCount(), 4);
        QCOMPARE(row(tree, QStringLiteral("ideas.md"))->text(0), QStringLiteral("M"));
        QCOMPARE(row(tree, QStringLiteral("journal.md"))->checkState(0), Qt::Checked);   // a new note
        QCOMPARE(row(tree, QStringLiteral("scratch.txt"))->checkState(0), Qt::Unchecked); // untracked, not a note
        QVERIFY(!dialog->commitButton()->isEnabled());                                     // no message yet
        dialog->messageEdit()->setPlainText(QStringLiteral("Plan and journal\n\nIdeas wait for another day."));
        QVERIFY(dialog->commitButton()->isEnabled());
        row(tree, QStringLiteral("ideas.md"))->setCheckState(0, Qt::Unchecked);
        QCOMPARE(dialog->selectedPaths(), (QStringList{QStringLiteral("journal.md"), QStringLiteral("plan.md")}));
    }

    void commitsWhatWasTickedAndPushes()
    {
        CommitDialog* dialog = m_window->commitDialog();
        dialog->pushBox()->setChecked(true);
        QSignalSpy committed(m_window.get(), &MainWindow::committed);
        QSignalSpy pushed(m_window.get(), &MainWindow::pushFinished);
        QSignalSpy status(m_window.get(), &MainWindow::gitStatusUpdated);
        dialog->commitButton()->click();
        QVERIFY(committed.wait(kWaitMs));
        QVERIFY(!dialog->isVisible());
        const QString id = committed.front().at(0).toString();

        auto git = GitRepository::discover(repo());
        QCOMPARE(GitFixture::resolve(repo(), "HEAD"), id);
        QCOMPARE(git->fileAt(id, QStringLiteral("plan.md")).value(), QByteArray("# Plan\n\nTyped, not saved.\n"));
        QVERIFY(git->fileAt(id, QStringLiteral("journal.md")));
        QCOMPARE(git->fileAt(id, QStringLiteral("ideas.md")).value(), QByteArray("# Ideas\n")); // left out
        // Left out, so still counted: ideas.md and scratch.txt.
        QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty() && status.last().at(0).toInt() == 2, kWaitMs);
        QCOMPARE(m_window->gitButton()->text(), QStringLiteral("● 2 file(s) changed"));

        QTRY_VERIFY_WITH_TIMEOUT(!pushed.isEmpty(), kWaitMs); // it may land while the test checks other things
        QVERIFY2(pushed.front().at(0).toBool(), qPrintable(pushed.front().at(1).toString()));
        QCOMPARE(GitFixture::resolve(remote(), "refs/heads/main"), id);
        QVERIFY(dialog->pushBox()->isChecked()); // remembered
    }

    void errorsStayInTheDialogOrTheStatusBar()
    {
        CommitDialog* dialog = openDialog();
        QVERIFY(dialog);
        // ideas.md changes back to HEAD's text after the dialog listed it.
        m_git->write(QStringLiteral("ideas.md"), "# Ideas\n");
        for (int i = 0; i < dialog->tree()->topLevelItemCount(); ++i)
            dialog->tree()->topLevelItem(i)->setCheckState(0, dialog->tree()->topLevelItem(i)->text(1) == QLatin1String("ideas.md") ? Qt::Checked : Qt::Unchecked);
        dialog->messageEdit()->setPlainText(QStringLiteral("Nothing really"));
        dialog->pushBox()->setChecked(false);
        dialog->commitButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(dialog->errorLabel()->isVisible(), kWaitMs);
        QVERIFY(dialog->errorLabel()->text().contains(QStringLiteral("unchanged")));
        QVERIFY(dialog->isVisible());          // still open, to fix and retry
        QVERIFY(dialog->commitButton()->isEnabled());
        dialog->hide();

        // A push that fails reports in the status bar; nothing blocks.
        QDir(remote()).removeRecursively();
        m_git->write(QStringLiteral("journal.md"), "New note, edited.\n");
        dialog = openDialog();
        for (int i = 0; i < dialog->tree()->topLevelItemCount(); ++i)
            dialog->tree()->topLevelItem(i)->setCheckState(0, dialog->tree()->topLevelItem(i)->text(1) == QLatin1String("journal.md") ? Qt::Checked : Qt::Unchecked);
        dialog->messageEdit()->setPlainText(QStringLiteral("Edit the journal"));
        dialog->pushBox()->setChecked(true);
        QSignalSpy pushed(m_window.get(), &MainWindow::pushFinished);
        dialog->commitButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(!pushed.isEmpty(), kWaitMs); // it may land while the test checks other things
        QCOMPARE(pushed.front().at(0).toBool(), false);
        QVERIFY(m_window->statusBar()->currentMessage().startsWith(QStringLiteral("Push failed: ")));
        QVERIFY(m_window->gitButton()->toolTip().startsWith(QStringLiteral("Last push failed")));
    }

    void cleanupTestCase()
    {
        if (m_window) {
            for (int i = 0; i < m_window->tabs()->count(); ++i) {
                if (Editor* editor = m_window->editorAt(i))
                    editor->document()->setModified(false);
            }
        }
        m_window.reset();
        m_git.reset();
    }
};

QTEST_MAIN(TestCommitDialog)
#include "test_commit_dialog.moc"
