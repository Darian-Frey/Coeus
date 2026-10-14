// F-032 step 2 in the window: Refresh header, Promote idea, and Fetch.

#include "../git/GitFixture.hpp"

#include "git/GitCommand.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/ProjectBoard.hpp"
#include "ui/PromoteDialog.hpp"
#include "vault/Roots.hpp"

#include <QAction>
#include <QDockWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr int kWaitMs = 15'000;

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QAction* menuAction(QMenu* menu, const QString& text)
{
    for (QAction* action : menu->actions()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

bool git(const QStringList& arguments)
{
    QProcess process;
    process.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("user.name=Other"), QStringLiteral("-c"),
                                                     QStringLiteral("user.email=other@example.org")} + arguments);
    return process.waitForFinished(30'000) && process.exitCode() == 0;
}

const QByteArray kStaleHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-01-01\n"
                                "> **Why this status:** T\n\n# Alpha\n";

} // namespace

class TestBoardCommands : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_dir;
    std::unique_ptr<GitFixture> m_git;
    std::unique_ptr<MainWindow> m_window;

    QString vault(const char* relative = "") const { return m_dir.filePath(QStringLiteral("vault/")) + QLatin1String(relative); }
    QString today() const { return QDate::currentDate().toString(Qt::ISODate); }

    void showBoard()
    {
        if (m_window->boardDock()->isVisible())
            return;
        QSignalSpy updated(m_window->board(), &ProjectBoard::updated);
        m_window->boardDock()->toggleViewAction()->trigger();
        QVERIFY(updated.wait(kWaitMs));
    }

    void waitForBoardRow(const QString& name)
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_window->board()->row(name) != nullptr, kWaitMs);
    }

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is not installed");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        const QString remote = m_dir.filePath(QStringLiteral("remote.git"));
        QVERIFY(GitFixture::initBare(remote));
        m_git = std::make_unique<GitFixture>(vault());
        m_git->setIdentity();
        m_git->setUpstream(remote);
        m_git->write(QStringLiteral(".gitignore"), ".coeus/\n");
        m_git->write(QStringLiteral("projects/alpha/README.md"), kStaleHeader);
        m_git->write(QStringLiteral("ideas/theia.md"), "---\nstage: scoped\ncodename: Theia\nsummary: A lamp\n---\n# Theia\n\nIt dims.\n");
        m_git->write(QStringLiteral("ideas/hermes.md"), "---\nstage: idea\ncodename: Hermes\n---\n# Hermes\n");
        m_git->commit(QStringLiteral("Start"), {QStringLiteral(".gitignore"), QStringLiteral("projects/alpha/README.md"),
                                                QStringLiteral("ideas/theia.md"), QStringLiteral("ideas/hermes.md")}, 1'790'000'000);
        QVERIFY(git({QStringLiteral("-C"), vault(), QStringLiteral("push"), QStringLiteral("--quiet")}));
        // Someone else pushes a commit: the vault is one behind once it fetches.
        const QString other = m_dir.filePath(QStringLiteral("other"));
        QVERIFY(git({QStringLiteral("clone"), QStringLiteral("--quiet"), QStringLiteral("--branch"), QStringLiteral("main"), remote, other}));
        QFile note(other + QStringLiteral("/elsewhere.md"));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("From another machine.\n");
        note.close();
        QVERIFY(git({QStringLiteral("-C"), other, QStringLiteral("add"), QStringLiteral("elsewhere.md")}));
        QVERIFY(git({QStringLiteral("-C"), other, QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), QStringLiteral("Elsewhere")}));
        QVERIFY(git({QStringLiteral("-C"), other, QStringLiteral("push"), QStringLiteral("--quiet")}));

        m_window = std::make_unique<MainWindow>();
        m_window->resize(1200, 800);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(vault()));
        QVERIFY(reloaded.wait(kWaitMs));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->catalogue()->ideas().size() == 2, kWaitMs);
        showBoard();
    }

    void fetchShowsWhatOthersPushed()
    {
        ProjectBoard* board = m_window->board();
        QCOMPARE(board->row(QStringLiteral("projects/alpha"))->text(ProjectBoard::Git), QStringLiteral("main ↑0 ↓0"));
        QSignalSpy fetched(board, &ProjectBoard::fetched);
        QSignalSpy updated(board, &ProjectBoard::updated);
        board->fetchButton()->click();
        QVERIFY(board->isFetching());
        QTRY_VERIFY_WITH_TIMEOUT(!fetched.isEmpty(), kWaitMs);
        QCOMPARE(fetched.front().at(0).toInt(), 1);
        QVERIFY(fetched.front().at(1).toStringList().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(board->row(QStringLiteral("projects/alpha"))->text(ProjectBoard::Git), QStringLiteral("main ↑0 ↓1"), kWaitMs);
        QCOMPARE(read(vault("projects/alpha/README.md")), kStaleHeader); // fetch touches no file
    }

    void refreshHeaderAfterConfirming()
    {
        ProjectBoard* board = m_window->board();
        QSignalSpy refreshed(m_window.get(), &MainWindow::headerRefreshed);
        menuAction(board->rowMenu(board->row(QStringLiteral("projects/alpha"))), QStringLiteral("Refresh Header…"))->trigger();
        auto* prompt = m_window->findChild<QMessageBox*>(QStringLiteral("refreshHeaderPrompt"));
        QVERIFY(prompt);
        QVERIFY(prompt->text().contains(today()));
        prompt->button(QMessageBox::Yes)->click();
        QTRY_VERIFY_WITH_TIMEOUT(!refreshed.isEmpty(), kWaitMs);
        QCOMPARE(read(vault("projects/alpha/README.md")), QByteArray(kStaleHeader).replace("2026-01-01", today().toLatin1()));
        QTRY_VERIFY_WITH_TIMEOUT(board->row(QStringLiteral("projects/alpha"))->text(ProjectBoard::Reviewed) == QStringLiteral("today"), kWaitMs);

        // Cancelled: nothing changes.
        QFile::remove(vault("projects/alpha/README.md"));
        m_git->write(QStringLiteral("projects/alpha/README.md"), kStaleHeader);
        menuAction(board->rowMenu(board->row(QStringLiteral("projects/alpha"))), QStringLiteral("Refresh Header…"))->trigger();
        m_window->findChild<QMessageBox*>(QStringLiteral("refreshHeaderPrompt"))->button(QMessageBox::Cancel)->click();
        QTest::qWait(50);
        QCOMPARE(read(vault("projects/alpha/README.md")), kStaleHeader);
    }

    void refreshHeaderInAnOpenBufferWithEdits()
    {
        m_window->openNote(vault("projects/alpha/README.md"));
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("Unsaved thought.\n"));
        QSignalSpy refreshed(m_window.get(), &MainWindow::headerRefreshed);
        m_window->refreshHeader(vault("projects/alpha/README.md"), QStringLiteral("projects/alpha"));
        m_window->findChild<QMessageBox*>(QStringLiteral("refreshHeaderPrompt"))->button(QMessageBox::Yes)->click();
        QTRY_VERIFY_WITH_TIMEOUT(!refreshed.isEmpty(), kWaitMs);
        QVERIFY(editor->toPlainText().contains(QStringLiteral("> **Last reviewed:** ") + today()));
        QVERIFY(editor->isModified());                                 // the author's edits are theirs to save
        QCOMPARE(read(vault("projects/alpha/README.md")), kStaleHeader); // so the file is untouched
        editor->document()->undo();                                     // one step: the header
        QVERIFY(editor->toPlainText().contains(QStringLiteral("2026-01-01")));
        editor->document()->undo();
        editor->document()->setModified(false);
    }

    void promoteIntoTheVault()
    {
        ProjectBoard* board = m_window->board();
        waitForBoardRow(QStringLiteral("Theia"));
        menuAction(board->rowMenu(board->row(QStringLiteral("Theia"))), QStringLiteral("Promote Idea…"))->trigger();
        auto* dialog = m_window->findChild<PromoteDialog*>(QStringLiteral("promoteDialog"));
        QVERIFY(dialog);
        QCOMPARE(dialog->codename(), QStringLiteral("Theia"));
        QCOMPARE(dialog->targetFolder(), vault("projects/Theia"));
        QVERIFY(dialog->promoteButton()->isEnabled());
        dialog->codenameEdit()->setText(QStringLiteral("../out"));
        QVERIFY(!dialog->promoteButton()->isEnabled()); // not a folder name
        dialog->codenameEdit()->setText(QStringLiteral("alpha"));
        dialog->codenameEdit()->setText(QStringLiteral("Theia"));
        QSignalSpy promoted(m_window.get(), &MainWindow::promoted);
        dialog->promoteButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(!promoted.isEmpty(), kWaitMs);

        const QString readme = vault("projects/Theia/README.md");
        QCOMPARE(promoted.front().at(0).toString(), readme);
        QVERIFY(!QFile::exists(vault("ideas/theia.md"))); // removed after the writes
        for (const char* name : {"FEATURES.md", "ROADMAP.md", "DECISIONS.md", "CLAUDE.md", "CHANGELOG.md"})
            QVERIFY2(QFile::exists(vault("projects/Theia/") + QLatin1String(name)), name);
        QVERIFY(read(readme).startsWith("> **Status:** Active\n"));
        QCOMPARE(m_window->currentEditor()->path(), readme);
        waitForBoardRow(QStringLiteral("projects/Theia"));
        QCOMPARE(board->row(QStringLiteral("projects/Theia"))->text(ProjectBoard::Build),
                 QStringLiteral("Phase 0 — Scaffolding · Not started (0 of 1 complete)"));
        QVERIFY(!board->row(QStringLiteral("Theia")) || board->row(QStringLiteral("Theia"))->parent() != board->ideasGroup());
        const auto& registers = m_window->catalogue()->registers();
        QVERIFY(std::none_of(registers.begin(), registers.end(), [](const RegisterRef& r) { return r.project == QLatin1String("projects/Theia"); }));

        // The same place again is refused: it exists.
        IdeaRef again{vault("ideas/hermes.md"), QStringLiteral("hermes"), QStringLiteral("idea"), QStringLiteral("Hermes"), {}, {}};
        QVERIFY(!m_window->promote(again, QStringLiteral("Theia"), vault("projects/Theia"), QString()));
        QVERIFY(QFile::exists(vault("ideas/hermes.md")));
    }

    void promoteToANewLinkedRoot()
    {
        ProjectBoard* board = m_window->board();
        waitForBoardRow(QStringLiteral("Hermes"));
        menuAction(board->rowMenu(board->row(QStringLiteral("Hermes"))), QStringLiteral("Promote Idea…"))->trigger();
        auto* dialog = m_window->findChild<PromoteDialog*>(QStringLiteral("promoteDialog"));
        QVERIFY(dialog);
        dialog->linkedChoice()->setChecked(true);
        dialog->parentEdit()->setText(vault("inside")); // a linked root may not lie in the vault
        QVERIFY(!dialog->promoteButton()->isEnabled());
        dialog->parentEdit()->setText(m_dir.filePath(QStringLiteral("repos")));
        QCOMPARE(dialog->targetFolder(), m_dir.filePath(QStringLiteral("repos/Hermes")));
        QCOMPARE(dialog->rootName(), QStringLiteral("hermes"));
        QSignalSpy promoted(m_window.get(), &MainWindow::promoted);
        dialog->promoteButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(!promoted.isEmpty(), kWaitMs);

        const auto roots = Roots::load(vault()).roots;
        QCOMPARE(roots.size(), 1);
        QCOMPARE(roots.front().name, QStringLiteral("hermes"));
        QCOMPARE(roots.front().path, m_dir.filePath(QStringLiteral("repos/Hermes")));
        QVERIFY(!QFile::exists(vault("ideas/hermes.md")));
        QCOMPARE(m_window->currentEditor()->path(), m_dir.filePath(QStringLiteral("repos/Hermes/README.md")));
        // Reopened with the new root: indexed, and on the board as a project of its own.
        waitForBoardRow(QStringLiteral("hermes"));
        QCOMPARE(m_window->board()->row(QStringLiteral("hermes"))->text(ProjectBoard::Git), QStringLiteral("not in Git"));
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

QTEST_MAIN(TestBoardCommands)
#include "test_board_commands.moc"
