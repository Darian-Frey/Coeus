// F-032 in the window: the project board over a vault that is a repository.

#include "../git/GitFixture.hpp"

#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/ProjectBoard.hpp"

#include <QAction>
#include <QDockWidget>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace coeus;
using coeus::testing::GitFixture;

namespace {

constexpr int kWaitMs = 10'000;

QByteArray header(const char* status, const QString& reviewed)
{
    return QByteArray("> **Status:** ") + status + "\n> **Provenance:** T\n> **Last reviewed:** " + reviewed.toLatin1()
           + "\n> **Why this status:** T\n\n# Project\n";
}

} // namespace

class TestProjectBoard : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_dir;
    std::unique_ptr<GitFixture> m_git;
    std::unique_ptr<MainWindow> m_window;

    QString vault(const char* relative) const { return m_dir.filePath(QStringLiteral("vault/")) + QLatin1String(relative); }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        m_git = std::make_unique<GitFixture>(m_dir.filePath(QStringLiteral("vault")));
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        m_git->write(QStringLiteral("projects/alpha/README.md"), header("Active (since September)", today));
        m_git->write(QStringLiteral("projects/alpha/ROADMAP.md"),
                     "## Phase 1 — Core\n**Status:** Complete\n\n## Phase 2 — Board\n**Status:** In progress\n");
        m_git->write(QStringLiteral("projects/alpha/docs/CHANGELOG.md"), "## [Unreleased]\n- One\n- Two\n\n## [0.1.0] - 2026-09-01\n- Old\n");
        m_git->write(QStringLiteral("projects/beta/README.md"), header("Dormant", QStringLiteral("2026-01-01")));
        m_git->write(QStringLiteral("ideas/theia.md"), "---\nstage: scoped\ncodename: Theia\nsummary: A lamp\n---\n# Theia\n");
        m_git->write(QStringLiteral("ideas/hermes.md"), "---\nstage: idea\n---\n# Hermes\n");
        m_git->commit(QStringLiteral("Start"), {QStringLiteral("projects/alpha/README.md"), QStringLiteral("projects/beta/README.md")},
                      1'790'000'000);
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1200, 800);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_dir.filePath(QStringLiteral("vault"))));
        QVERIFY(reloaded.wait(kWaitMs));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->catalogue()->ideas().size() == 2, kWaitMs);
    }

    void shownFromTheKeyboard()
    {
        QAction* toggle = m_window->boardDock()->toggleViewAction();
        QCOMPARE(toggle->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
        QVERIFY(m_window->registeredActions().contains(toggle));
        QVERIFY(!m_window->boardDock()->isVisible());
        QSignalSpy updated(m_window->board(), &ProjectBoard::updated);
        toggle->trigger();
        QVERIFY(m_window->boardDock()->isVisible());
        QVERIFY(updated.wait(kWaitMs));
        QCOMPARE(updated.last().at(0).toInt(), 2);
        QCOMPARE(updated.last().at(1).toInt(), 2);
    }

    void projectsByStatusWithTheirColumns()
    {
        ProjectBoard* board = m_window->board();
        QTreeWidgetItem* projects = board->projectsGroup();
        QCOMPARE(projects->text(ProjectBoard::Name), QStringLiteral("Projects (2)"));
        QCOMPARE(projects->child(0)->text(ProjectBoard::Name), QStringLiteral("projects/alpha")); // Active before Dormant
        QTreeWidgetItem* alpha = board->row(QStringLiteral("projects/alpha"));
        QCOMPARE(alpha->text(ProjectBoard::Status), QStringLiteral("Active"));
        QCOMPARE(alpha->text(ProjectBoard::Reviewed), QStringLiteral("today"));
        QCOMPARE(alpha->text(ProjectBoard::Build), QStringLiteral("Phase 2 — Board · In progress (1 of 2 complete)"));
        QCOMPARE(alpha->text(ProjectBoard::Unreleased), QStringLiteral("2"));
        QVERIFY(alpha->toolTip(ProjectBoard::Unreleased).contains(QStringLiteral("0.1.0")));
        // ROADMAP.md and docs/CHANGELOG.md were never committed: two changed files in alpha.
        QCOMPARE(alpha->text(ProjectBoard::Git), QStringLiteral("main · ●2"));
        QVERIFY(alpha->toolTip(ProjectBoard::Git).startsWith(QStringLiteral("No upstream branch")));

        QTreeWidgetItem* beta = board->row(QStringLiteral("projects/beta"));
        QVERIFY(beta->text(ProjectBoard::Reviewed).endsWith(QStringLiteral(" d")));
        QVERIFY(beta->foreground(ProjectBoard::Reviewed) != alpha->foreground(ProjectBoard::Reviewed)); // stale
        QCOMPARE(beta->text(ProjectBoard::Build), QStringLiteral("no ROADMAP"));
        QCOMPARE(beta->text(ProjectBoard::Unreleased), QStringLiteral("—"));
        QCOMPARE(beta->text(ProjectBoard::Git), QStringLiteral("main"));
    }

    void ideasByStage()
    {
        ProjectBoard* board = m_window->board();
        QTreeWidgetItem* ideas = board->ideasGroup();
        QCOMPARE(ideas->text(ProjectBoard::Name), QStringLiteral("Ideas (2)"));
        QCOMPARE(ideas->child(0)->text(ProjectBoard::Name), QStringLiteral("hermes")); // idea before scoped
        QCOMPARE(ideas->child(1)->text(ProjectBoard::Name), QStringLiteral("Theia"));
        QCOMPARE(ideas->child(1)->text(ProjectBoard::Status), QStringLiteral("scoped"));
        QCOMPARE(ideas->child(1)->text(ProjectBoard::Build), QStringLiteral("A lamp"));
    }

    void rowsOpenTheirNote()
    {
        QTreeWidget* tree = m_window->board()->tree();
        emit tree->itemClicked(m_window->board()->row(QStringLiteral("projects/beta")), 0);
        QCOMPARE(m_window->currentEditor()->path(), vault("projects/beta/README.md"));
        emit tree->itemClicked(m_window->board()->row(QStringLiteral("Theia")), 0);
        QCOMPARE(m_window->currentEditor()->path(), vault("ideas/theia.md"));
        emit tree->itemClicked(m_window->board()->projectsGroup(), 0); // a group opens nothing
        QCOMPARE(m_window->currentEditor()->path(), vault("ideas/theia.md"));
    }

    void followsTheVault()
    {
        // A new idea appears once the index has it.
        QSignalSpy updated(m_window->board(), &ProjectBoard::updated);
        m_git->write(QStringLiteral("ideas/athena.md"), "---\nstage: building\n---\n# Athena\n");
        QTRY_VERIFY_WITH_TIMEOUT(!updated.isEmpty() && updated.last().at(1).toInt() == 3, kWaitMs);
        QCOMPARE(m_window->board()->ideasGroup()->child(2)->text(ProjectBoard::Name), QStringLiteral("athena"));
    }

    void cleanupTestCase()
    {
        m_window.reset();
        m_git.reset();
    }
};

QTEST_MAIN(TestProjectBoard)
#include "test_project_board.moc"
