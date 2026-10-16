// F-014 in the window: starring notes and searches, the Bookmarks pane, and
// recent notes in the palette and File › Open Recent, kept per vault.

#include "ui/BookmarksPanel.hpp"
#include "ui/CommandPalette.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/VaultSession.hpp"
#include "ui/NoteView.hpp"
#include "ui/SearchPanel.hpp"

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

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

QStringList rows(QTreeWidget* tree, int column = 0)
{
    QStringList texts;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        texts.append(tree->topLevelItem(i)->text(column));
    return texts;
}

} // namespace

class TestBookmarksWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    QTemporaryDir m_repo;

    QString note(const char* path) const { return m_vault.filePath(QString::fromLatin1(path)); }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        write(note("plan.md"), "# Plan\n");
        write(note("notes/ideas.md"), "# Ideas\n");
        write(note("journal.md"), "# Journal\n");
        write(m_repo.filePath(QStringLiteral("DECISIONS.md")), "# Decisions\n");
        write(note(".coeus/config.toml"),
              QStringLiteral("[[roots]]\nname = \"repo\"\npath = \"%1\"\n").arg(m_repo.path()).toUtf8());
    }

    void starNotesAndSearches()
    {
        MainWindow window;
        window.openVault(*Vault::open(m_vault.path()));
        QAction* toggle = actionNamed(window, QStringLiteral("Toggle Bookmark"));
        QVERIFY(toggle);
        QCOMPARE(toggle->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));

        window.openNote(note("plan.md"));
        QCOMPARE(window.currentView()->starButton()->text(), QStringLiteral("☆"));
        toggle->trigger();
        QCOMPARE(window.currentView()->starButton()->text(), QStringLiteral("★"));
        // The header's star does the same; a linked root's note is named by its root.
        window.openNote(m_repo.filePath(QStringLiteral("DECISIONS.md")));
        window.currentView()->starButton()->click();
        QCOMPARE(window.bookmarks(), (QList<Bookmark>{Bookmark::note(QStringLiteral("vault"), QStringLiteral("plan.md")),
                                                      Bookmark::note(QStringLiteral("repo"), QStringLiteral("DECISIONS.md"))}));

        // A search, with its mode.
        SearchPanel* search = window.findChild<SearchPanel*>();
        QVERIFY(!search->starButton()->isEnabled()); // nothing to star yet
        search->setMode(SearchPanel::Mode::Meaning);
        search->setQuery(QStringLiteral("what to build next"));
        QVERIFY(search->starButton()->isEnabled());
        search->starButton()->click();
        QCOMPARE(search->starButton()->text(), QStringLiteral("★"));
        search->setMode(SearchPanel::Mode::Words); // another search: not starred
        QCOMPARE(search->starButton()->text(), QStringLiteral("☆"));

        BookmarksPanel* panel = window.bookmarksPanel();
        QCOMPARE(rows(panel->tree()), (QStringList{QStringLiteral("★ plan"), QStringLiteral("★ DECISIONS"),
                                                  QStringLiteral("⌕ what to build next")}));
        QCOMPARE(rows(panel->tree(), 1), (QStringList{QStringLiteral("plan.md"), QStringLiteral("repo/DECISIONS.md"),
                                                     QStringLiteral("by meaning")}));
        QVERIFY(QFile::exists(note(".coeus/bookmarks.json")));
    }

    void thePaneOpensMovesAndRemoves()
    {
        MainWindow window;
        window.openVault(*Vault::open(m_vault.path()));
        BookmarksPanel* panel = window.bookmarksPanel();
        QCOMPARE(panel->tree()->topLevelItemCount(), 3); // loaded from .coeus/bookmarks.json

        emit panel->activated(1);
        QCOMPARE(window.currentView()->path(), m_repo.filePath(QStringLiteral("DECISIONS.md")));
        QCOMPARE(window.currentView()->starButton()->text(), QStringLiteral("★"));
        emit panel->activated(2); // the search runs in its mode
        SearchPanel* search = window.findChild<SearchPanel*>();
        QCOMPARE(search->input()->text(), QStringLiteral("what to build next"));
        QCOMPARE(search->mode(), SearchPanel::Mode::Meaning);

        emit panel->moveRequested(2, 0);
        QCOMPARE(window.bookmarks().front().kind, Bookmark::Kind::Search);
        emit panel->removeRequested(0);
        QCOMPARE(window.bookmarks().size(), 2);
        QCOMPARE(Bookmarks::load(m_vault.path()).size(), 2);

        // A note deleted since stays listed, struck through, until removed.
        write(note("gone.md"), "# Gone\n");
        window.openNote(note("gone.md"));
        window.toggleBookmark(note("gone.md"));
        window.closeCurrentTab();
        QFile::remove(note("gone.md"));
        window.openVault(*Vault::open(m_vault.path()));
        QTreeWidgetItem* gone = panel->tree()->topLevelItem(2);
        QCOMPARE(gone->text(0), QStringLiteral("★ gone"));
        QVERIFY(gone->font(0).strikeOut());
        emit panel->activated(2);
        QVERIFY(!window.currentView() || window.currentView()->path() != note("gone.md"));
    }

    void recentNotesNewestFirst()
    {
        QFile::remove(note(".coeus/recent.json"));
        auto window = std::make_unique<MainWindow>();
        window->openVault(*Vault::open(m_vault.path()));
        for (const char* path : {"plan.md", "notes/ideas.md", "journal.md"})
            window->openNote(note(path));
        QCOMPARE(window->recentNotes().front(), (RecentNotes::Note{QStringLiteral("vault"), QStringLiteral("journal.md")}));
        QCOMPARE(window->recentNotes()[1], (RecentNotes::Note{QStringLiteral("vault"), QStringLiteral("notes/ideas.md")}));

        QAction* recent = actionNamed(*window, QStringLiteral("Open Recent Note…"));
        QVERIFY(recent);
        QCOMPARE(recent->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_R));
        recent->trigger();
        // The note in front goes last: the one before it is the likelier choice.
        const QStringList offered = rows(window->recentPicker()->findChild<QTreeWidget*>());
        QCOMPARE(offered.mid(0, 2), (QStringList{QStringLiteral("notes/ideas.md"), QStringLiteral("plan.md")}));
        QVERIFY(offered.contains(QStringLiteral("journal.md")));
        QCOMPARE(offered.indexOf(QStringLiteral("journal.md")), offered.size() - 1);
        window->recentPicker()->close();

        // Written a moment after they settle, without waiting for the window to close (BUG-012).
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(note(".coeus/recent.json")), VaultSession::kRecentSaveDelayMs + 3000);
        // Kept across a restart, in this vault's .coeus/.
        QFile::remove(note(".coeus/recent.json"));
        window->openNote(note("plan.md"));
        window->openNote(note("journal.md"));
        window->close();
        QVERIFY(QFile::exists(note(".coeus/recent.json")));
        window = std::make_unique<MainWindow>();
        window->openVault(*Vault::open(m_vault.path()));
        QCOMPARE(window->recentNotes().front(), (RecentNotes::Note{QStringLiteral("vault"), QStringLiteral("journal.md")}));
    }

    void noShortcutIsShared()
    {
        MainWindow window;
        const QList<QAction*> actions = window.registeredActions();
        for (QAction* action : actions)
            for (QAction* other : actions)
                if (other != action && !action->shortcut().isEmpty())
                    QVERIFY2(other->shortcut() != action->shortcut(), qPrintable(action->text() + QStringLiteral(" / ") + other->text()));
    }
};

QTEST_MAIN(TestBookmarksWindow)
#include "test_bookmarks_window.moc"
