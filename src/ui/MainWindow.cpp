#include "ui/MainWindow.hpp"

#include "ui/BacklinksPanel.hpp"
#include "ui/CommandPalette.hpp"
#include "ui/CommitDialog.hpp"
#include "ui/DiffView.hpp"
#include "ui/Editor.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/Icons.hpp"
#include "ui/McpLink.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/NoteView.hpp"
#include "ui/OutlinePanel.hpp"
#include "git/GitCommand.hpp"
#include "git/GitRepository.hpp"
#include "index/ContentHash.hpp"
#include "index/IndexQueries.hpp"
#include "index/IndexService.hpp"
#include "search/EmbeddingService.hpp"
#include "parser/RegisterParser.hpp"
#include "registers/Lint.hpp"
#include "registers/NextId.hpp"
#include "registers/ProjectScaffold.hpp"
#include "registers/RegisterTemplates.hpp"
#include "ui/ProblemsPanel.hpp"
#include "ui/ProjectBoard.hpp"
#include "ui/NewNoteDialog.hpp"
#include "ui/PromoteDialog.hpp"
#include "ui/Theme.hpp"
#include "ui/ShortcutsDialog.hpp"
#include "ui/Shortcuts.hpp"
#include "ui/RegisterPanel.hpp"
#include "ui/RelatedPanel.hpp"
#include "ui/WikilinkCompletion.hpp"
#include "ui/SearchPanel.hpp"
#include "ui/SidebarPanel.hpp"
#include "ui/TagPanel.hpp"
#include "ui/VersionView.hpp"
#include "ui/WriteToast.hpp"
#include "vault/DailyNotes.hpp"
#include "vault/GitIgnoreOffer.hpp"
#include "vault/Journal.hpp"
#include "vault/NewNote.hpp"
#include "vault/Roots.hpp"
#include "vault/TempSweeper.hpp"
#include "vault/Templates.hpp"
#include "vault/VaultConfig.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QUrl>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QActionGroup>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

// Bump when docks or toolbars change identity so stale layouts are ignored.
constexpr int kLayoutVersion = 1;
constexpr int kStatusTimeoutMs = 5000;
constexpr int kGitStatusDelayMs = 400; // after saves and tab changes settle
constexpr int kBoardDelayMs = 1000;    // after the index settles
constexpr int kRelatedDelayMs = 800;   // after embedding progress settles

const char* const kPathProperty = "coeusPath";
const char* const kDiffProperty = "coeusDiff"; // identifies a DiffView tab
const char* const kVersionProperty = "coeusVersion"; // "<absolute path>@<commit id>" on a VersionView tab

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_palette(new CommandPalette(this))
{
    Theme::instance().initialise(); // the saved theme and snippets, before any widget is styled (F-027)
    setWindowTitle(QStringLiteral("Coeus"));
    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeTab(index); });

    m_catalogue = new NoteCatalogue(this);

    m_journalPool = new QThreadPool(this);
    m_journalPool->setMaxThreadCount(1);
    // The Git worker (ARCHITECTURE.md threading): libgit2 calls, one at a time.
    m_gitPool = new QThreadPool(this);
    m_gitPool->setMaxThreadCount(1);
    m_push = new GitCommand(this);
    connect(m_push, &GitCommand::finished, this, [this](bool ok, const QString& result) {
        const QString message = ok ? (result.isEmpty() ? tr("Pushed") : tr("Pushed: %1").arg(result)) : result;
        // A failed push is shown, never thrown at the author in a dialog (F-022).
        statusBar()->showMessage(ok ? message : tr("Push failed: %1").arg(message), kStatusTimeoutMs * (ok ? 2 : 6));
        m_gitButton->setToolTip(ok ? message : tr("Last push failed: %1").arg(message));
        refreshGitStatus();
        emit pushFinished(ok, message);
    });
    // MCP writes (F-033, AV-012): `coeus --mcp` asks before touching a note open
    // here and reports each write, which this window offers to undo.
    m_mcpLink = new McpLink(
        [this](const QString& path) {
            const Editor* editor = editorFor(path);
            if (!editor)
                editor = editorFor(QFileInfo(path).canonicalFilePath());
            if (!editor)
                return McpLink::Buffer{};
            return McpLink::Buffer{true, editor->isModified(), editor->isModified() ? editor->toPlainText() : QString()};
        },
        [this](const QString& path, const std::optional<QByteArray>& before, const QByteArray& afterHash, const QString& summary) {
            showWriteToast(path, before, afterHash, summary);
        },
        this);
    m_gitStatusTimer = new QTimer(this);
    m_gitStatusTimer->setSingleShot(true);
    m_gitStatusTimer->setInterval(kGitStatusDelayMs);
    connect(m_gitStatusTimer, &QTimer::timeout, this, &MainWindow::refreshGitStatus);
    m_journalTimer = new QTimer(this);
    m_journalTimer->setInterval(kJournalIntervalMs);
    connect(m_journalTimer, &QTimer::timeout, this, &MainWindow::journalNow);
    m_journalTimer->start();
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatus);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        Editor* editor = currentEditor();
        m_backlinks->showNote(editor ? editor->path() : QString());
        m_related->showNote(editor ? editor->path() : QString());
        // A version or a diff keeps its note's history in view.
        QString historyOf = editor ? editor->path() : QString();
        if (const auto* version = qobject_cast<VersionView*>(m_tabs->currentWidget()))
            historyOf = version->notePath();
        else if (const auto* diff = qobject_cast<DiffView*>(m_tabs->currentWidget()))
            historyOf = diff->notePath();
        m_history->showNote(historyOf);
        m_history->setBlameChecked(editor && editor->blameShown());
        m_gitStatusTimer->start(); // the count is the current note's repository's
        m_outline->setView(currentView());
        if (editor) {
            m_registers->followNote(editor->path());
            m_problems->followNote(editor->path());
        }
        updateProblemsButton();
    });

    createDocks();
    createActions();
    createStatusBar();
    restoreLayout();

    // Lists that colour their rows when filled are filled again in the new theme (F-027).
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        m_problems->refresh();
        m_registers->refresh();
        m_related->refresh();
        m_history->refresh();
        m_tags->reload();
    });
}

void MainWindow::createDocks()
{
    m_explorer = new ExplorerPanel;
    m_explorerDock = new QDockWidget(tr("Explorer"), this);
    m_explorerDock->setObjectName(QStringLiteral("explorerDock"));
    m_explorerDock->setWidget(m_explorer);
    addDockWidget(Qt::LeftDockWidgetArea, m_explorerDock);

    m_sidebar = new SidebarPanel;
    m_backlinks = new BacklinksPanel;
    m_sidebar->addSection(tr("Backlinks"), m_backlinks);
    connect(m_backlinks, &BacklinksPanel::openRequested, this, &MainWindow::openNoteAt);
    // Related notes (F-019), between Backlinks and History as in the mockup.
    m_related = new RelatedPanel;
    m_sidebar->addSection(tr("Related"), m_related);
    connect(m_related, &RelatedPanel::openRequested, this, &MainWindow::openNoteAt);
    connect(m_related, &RelatedPanel::linkRequested, this, &MainWindow::linkHere);
    m_relatedTimer = new QTimer(this);
    m_relatedTimer->setSingleShot(true);
    m_relatedTimer->setInterval(kRelatedDelayMs);
    connect(m_relatedTimer, &QTimer::timeout, m_related, &RelatedPanel::refresh);
    m_history = new HistoryPanel(m_gitPool);
    m_sidebar->addSection(tr("History"), m_history);
    connect(m_history, &HistoryPanel::versionRequested, this, &MainWindow::openVersion);
    connect(m_history, &HistoryPanel::diffWithHeadRequested, this, &MainWindow::diffWithHead);
    connect(m_history, &HistoryPanel::blameToggled, this, &MainWindow::setBlameShown);
    connect(m_history, &HistoryPanel::compareRequested, this,
            [this](const QString& notePath, const std::optional<GitCommit>& older, const std::optional<GitCommit>& newer) {
                const auto side = [](const std::optional<GitCommit>& commit, DiffSide::Kind absent, const QString& absentLabel) {
                    return commit ? DiffSide{DiffSide::Kind::Revision, commit->id, commit->path, commit->shortId}
                                  : DiffSide{absent, QString(), QString(), absentLabel};
                };
                openDiff(notePath, side(older, DiffSide::Kind::Nothing, tr("nothing")),
                         side(newer, DiffSide::Kind::WorkingCopy, tr("working copy")));
            });
    m_outline = new OutlinePanel;
    m_sidebar->addSection(tr("Outline"), m_outline);
    m_sidebarDock = new QDockWidget(tr("Sidebar"), this);
    m_sidebarDock->setObjectName(QStringLiteral("sidebarDock"));
    m_sidebarDock->setWidget(m_sidebar);
    addDockWidget(Qt::RightDockWidgetArea, m_sidebarDock);

    // Tags share the left area with the explorer, as tabs (mockup's tag icon).
    m_tags = new TagPanel;
    m_tagsDock = new QDockWidget(tr("Tags"), this);
    m_tagsDock->setObjectName(QStringLiteral("tagsDock"));
    m_tagsDock->setWidget(m_tags);
    addDockWidget(Qt::LeftDockWidgetArea, m_tagsDock);
    tabifyDockWidget(m_explorerDock, m_tagsDock);

    // Search joins them as a third tab (mockup's magnifier).
    m_search = new SearchPanel;
    m_searchDock = new QDockWidget(tr("Search"), this);
    m_searchDock->setObjectName(QStringLiteral("searchDock"));
    m_searchDock->setWidget(m_search);
    addDockWidget(Qt::LeftDockWidgetArea, m_searchDock);
    tabifyDockWidget(m_explorerDock, m_searchDock);
    connect(m_search, &SearchPanel::openRequested, this, &MainWindow::openNoteAtWords);
    connect(m_search, &SearchPanel::openAtLineRequested, this, &MainWindow::openNoteAt);

    // Registers: the scaffold standard's entries for the current project (F-015).
    m_registers = new RegisterPanel(m_catalogue);
    m_registersDock = new QDockWidget(tr("Registers"), this);
    m_registersDock->setObjectName(QStringLiteral("registersDock"));
    m_registersDock->setWidget(m_registers);
    addDockWidget(Qt::LeftDockWidgetArea, m_registersDock);
    tabifyDockWidget(m_explorerDock, m_registersDock);
    connect(m_registers, &RegisterPanel::openRequested, this, &MainWindow::openNoteAt);
    connect(m_catalogue, &NoteCatalogue::reloaded, m_registers, [this] {
        m_registers->refresh();
        if (Editor* editor = currentEditor())
            m_registers->followNote(editor->path());
    });
    m_explorerDock->raise();

    // Problems: the register lint for the current project (F-016), below the editor.
    m_problems = new ProblemsPanel(m_catalogue);
    m_problemsDock = new QDockWidget(tr("Problems"), this);
    m_problemsDock->setObjectName(QStringLiteral("problemsDock"));
    m_problemsDock->setWidget(m_problems);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
    m_problemsDock->hide(); // the status bar's count opens it
    connect(m_problems, &ProblemsPanel::openRequested, this, &MainWindow::openNoteAt);
    connect(m_problems, &ProblemsPanel::reportCopied, this, [this](int problems) {
        statusBar()->showMessage(tr("Copied a report of %n problem(s) as Markdown", nullptr, problems), kStatusTimeoutMs);
    });
    connect(m_catalogue, &NoteCatalogue::reloaded, m_problems, [this] {
        if (Editor* editor = currentEditor())
            m_problems->followNote(editor->path());
        m_problems->refresh();
        updateProblemsButton();
    });
    // The project board (F-032): below the editor with Problems, shown on request.
    m_board = new ProjectBoard(m_catalogue, m_gitPool);
    m_boardDock = new QDockWidget(tr("Project Board"), this);
    m_boardDock->setObjectName(QStringLiteral("boardDock"));
    m_boardDock->setWidget(m_board);
    addDockWidget(Qt::BottomDockWidgetArea, m_boardDock);
    tabifyDockWidget(m_problemsDock, m_boardDock);
    m_boardDock->hide();
    connect(m_board, &ProjectBoard::openRequested, this, &MainWindow::openNoteAt);
    connect(m_board, &ProjectBoard::refreshHeaderRequested, this, &MainWindow::refreshHeader);
    connect(m_board, &ProjectBoard::promoteRequested, this, &MainWindow::promoteIdea);
    connect(m_board, &ProjectBoard::fetched, this, [this](int repositories, const QStringList& failures) {
        // Failures are reported, never thrown at the author in a dialog.
        statusBar()->showMessage(failures.isEmpty() ? tr("Fetched %n repositor(y/ies)", nullptr, repositories)
                                                    : tr("Fetched %1 of %2; %3").arg(repositories - failures.size()).arg(repositories)
                                                          .arg(failures.join(QStringLiteral("; "))),
                                 kStatusTimeoutMs * (failures.isEmpty() ? 2 : 6));
    });
    m_boardTimer = new QTimer(this);
    m_boardTimer->setSingleShot(true);
    m_boardTimer->setInterval(kBoardDelayMs);
    connect(m_boardTimer, &QTimer::timeout, m_board, &ProjectBoard::refresh);
    connect(m_boardDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible)
            m_board->refresh();
    });
    connect(m_catalogue, &NoteCatalogue::reloaded, this, [this] {
        if (m_boardDock->isVisible())
            m_boardTimer->start(); // documents and Git are read once edits settle
    });
    connect(m_tags, &TagPanel::tagActivated, this, &MainWindow::showTag);
    connect(m_tags, &TagPanel::reloaded, this, [this] {
        if (!m_activeTag.isEmpty() && m_explorer->isFiltered())
            showTag(m_activeTag); // keep the filtered list current
    });

    resizeDocks({m_explorerDock, m_sidebarDock}, {260, 300}, Qt::Horizontal);

    connect(m_explorer, &ExplorerPanel::fileActivated, this, &MainWindow::openNote);
    connect(m_explorer, &ExplorerPanel::filterCleared, this, [this] { m_activeTag.clear(); });
}

void MainWindow::showTag(const QString& tag)
{
    if (tag.isEmpty())
        return;
    QList<std::pair<QString, QString>> notes;
    for (const Tags::Usage& usage : m_tags->notesFor(tag))
        notes.append({usage.notePath, usage.noteDisplay});
    m_activeTag = tag;
    m_explorer->setFilter(QLatin1Char('#') + tag, notes);
    m_explorerDock->show();
    m_explorerDock->raise();
}

void MainWindow::createActions()
{
    auto* openVault = new QAction(QIcon::fromTheme(QStringLiteral("document-open-folder")),
                                  tr("&Open Vault…"), this);
    openVault->setShortcut(QKeySequence::Open);
    connect(openVault, &QAction::triggered, this, &MainWindow::chooseVault);

    m_save = new QAction(QIcon::fromTheme(QStringLiteral("document-save")), tr("&Save"), this);
    m_save->setShortcut(QKeySequence::Save);
    connect(m_save, &QAction::triggered, this, &MainWindow::saveCurrent);

    auto* today = new QAction(QIcon::fromTheme(QStringLiteral("view-calendar-day")), tr("Open &Today's Note"), this);
    today->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(today, &QAction::triggered, this, &MainWindow::openTodaysNote);

    auto* insertNextIdAction = new QAction(tr("Insert &Next ID…"), this);
    insertNextIdAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(insertNextIdAction, &QAction::triggered, this, &MainWindow::chooseNextId);
    m_nextIdPicker = new CommandPalette(this);
    m_nextIdPicker->setPlaceholder(tr("Which register?"));
    m_nextIdPicker->setKeepsOrder(true); // the current register's kind first

    auto* newNoteAction = new QAction(QIcon::fromTheme(QStringLiteral("document-new")), tr("&New Note…"), this);
    newNoteAction->setShortcut(QKeySequence::New);
    connect(newNoteAction, &QAction::triggered, this, &MainWindow::newNote);

    auto* insertTemplateAction = new QAction(tr("Insert &Template…"), this);
    connect(insertTemplateAction, &QAction::triggered, this, &MainWindow::chooseTemplate);
    m_templatePicker = new CommandPalette(this);
    m_templatePicker->setPlaceholder(tr("Choose a template…"));

    m_toggleReading = new QAction(tr("Toggle &Reading Mode"), this);
    m_toggleReading->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_toggleReading, &QAction::triggered, this, &MainWindow::toggleReadingMode);

    auto* closeTab = new QAction(tr("&Close Tab"), this);
    closeTab->setShortcut(QKeySequence::Close);
    connect(closeTab, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    auto* quit = new QAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("&Quit"), this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    auto* palette = new QAction(Icons::palette(),
                                tr("Command &Palette…"), this);
    palette->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(palette, &QAction::triggered, this, &MainWindow::showCommandPalette);

    QAction* toggleExplorer = m_explorerDock->toggleViewAction();
    toggleExplorer->setText(tr("Toggle &Explorer"));
    toggleExplorer->setIcon(Icons::explorer());
    toggleExplorer->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

    QAction* toggleSidebar = m_sidebarDock->toggleViewAction();
    toggleSidebar->setText(tr("Toggle &Sidebar"));
    toggleSidebar->setIcon(Icons::sidebar());
    toggleSidebar->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));

    m_verifyIndex = new QAction(tr("&Verify Index"), this);
    m_verifyIndex->setEnabled(false);
    connect(m_verifyIndex, &QAction::triggered, this, [this] {
        if (m_index)
            QMetaObject::invokeMethod(m_index, &IndexService::verify, Qt::QueuedConnection);
    });

    m_rebuildIndex = new QAction(tr("&Rebuild Index"), this);
    m_rebuildIndex->setEnabled(false);
    connect(m_rebuildIndex, &QAction::triggered, this, [this] {
        if (m_index)
            QMetaObject::invokeMethod(m_index, &IndexService::rebuild, Qt::QueuedConnection);
    });

    m_compactSidecar = new QAction(tr("Compact &Embeddings Sidecar"), this);
    m_compactSidecar->setEnabled(false);
    connect(m_compactSidecar, &QAction::triggered, this, [this] {
        if (m_embedding)
            QMetaObject::invokeMethod(m_embedding, &EmbeddingService::compactSidecar, Qt::QueuedConnection);
    });

    m_gitignoreAction = new QAction(tr("Add Coeus Files to .&gitignore"), this);
    m_gitignoreAction->setEnabled(false);
    connect(m_gitignoreAction, &QAction::triggered, this, [this] {
        if (m_vault)
            checkGitignore(m_vault->rootPath(), false);
    });

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openVault);
    fileMenu->addAction(newNoteAction);
    fileMenu->addAction(today);
    fileMenu->addAction(insertTemplateAction);
    fileMenu->addAction(insertNextIdAction);
    fileMenu->addAction(m_save);
    fileMenu->addAction(closeTab);
    fileMenu->addSeparator();
    fileMenu->addAction(m_verifyIndex);
    fileMenu->addAction(m_rebuildIndex);
    fileMenu->addAction(m_compactSidecar);
    fileMenu->addAction(m_gitignoreAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quit);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(palette);
    viewMenu->addAction(m_toggleReading);
    viewMenu->addSeparator();
    viewMenu->addAction(toggleExplorer);
    viewMenu->addAction(toggleSidebar);

    // Appearance and keys (F-027).
    QMenu* themeMenu = viewMenu->addMenu(tr("T&heme"));
    auto* themes = new QActionGroup(this);
    for (const auto& [mode, label] : {std::pair{Theme::Mode::System, tr("System")}, std::pair{Theme::Mode::Light, tr("Light")},
                                      std::pair{Theme::Mode::Dark, tr("Dark")}, std::pair{Theme::Mode::Cassette, tr("Cassette")}}) {
        auto* action = new QAction(tr("Theme: %1").arg(label), themes);
        action->setCheckable(true);
        action->setChecked(Theme::instance().mode() == mode);
        connect(action, &QAction::triggered, this, [mode] { Theme::instance().setMode(mode); });
        themeMenu->addAction(action);
        m_themeActions.append(action);
    }
    themeMenu->addSeparator();
    auto* reloadSnippets = new QAction(tr("Reload Style Snippets"), this);
    connect(reloadSnippets, &QAction::triggered, this, [this] {
        statusBar()->showMessage(Theme::instance().reloadSnippets().join(QStringLiteral("; ")), kStatusTimeoutMs * 2);
    });
    auto* openSnippets = new QAction(tr("Open Snippets Folder"), this);
    connect(openSnippets, &QAction::triggered, this, [] {
        QDir().mkpath(Theme::snippetsFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(Theme::snippetsFolder()));
    });
    themeMenu->addAction(reloadSnippets);
    themeMenu->addAction(openSnippets);
    auto* shortcuts = new QAction(tr("&Keyboard Shortcuts…"), this);
    connect(shortcuts, &QAction::triggered, this, &MainWindow::editShortcuts);
    viewMenu->addAction(shortcuts);
    for (QAction* action : std::as_const(m_themeActions))
        registerAction(action);
    for (QAction* action : {reloadSnippets, openSnippets, shortcuts}) {
        addAction(action);
        registerAction(action);
    }

    // Git (F-021): the current note against HEAD, and the blame gutter.
    auto* diffHead = new QAction(tr("&Diff with HEAD"), this);
    diffHead->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
    connect(diffHead, &QAction::triggered, this, [this] {
        if (Editor* editor = currentEditor())
            diffWithHead(editor->path());
    });
    auto* toggleBlame = new QAction(tr("Toggle &Blame"), this);
    toggleBlame->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
    connect(toggleBlame, &QAction::triggered, this, [this] {
        if (Editor* editor = currentEditor())
            setBlameShown(!editor->blameShown());
    });
    auto* commitAction = new QAction(tr("&Commit…"), this);
    commitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    connect(commitAction, &QAction::triggered, this, &MainWindow::openCommitDialog);
    QMenu* gitMenu = menuBar()->addMenu(tr("&Git"));
    gitMenu->addAction(commitAction);
    gitMenu->addSeparator();
    gitMenu->addAction(diffHead);
    gitMenu->addAction(toggleBlame);
    for (QAction* action : {commitAction, diffHead, toggleBlame}) {
        addAction(action);
        registerAction(action);
    }

    auto* strip = new QToolBar(tr("Tools"), this);
    strip->setObjectName(QStringLiteral("toolStrip"));
    strip->setMovable(false);
    strip->setIconSize(QSize(18, 18));
    strip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    strip->addAction(toggleExplorer);
    auto* findNotes = new QAction(Icons::search(), tr("Search &Notes…"), this);
    findNotes->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(findNotes, &QAction::triggered, this, [this] {
        m_searchDock->show();
        m_searchDock->raise();
        m_search->input()->setFocus();
        m_search->input()->selectAll();
    });
    strip->addAction(findNotes);
    viewMenu->addAction(findNotes);
    addAction(findNotes);
    registerAction(findNotes);

    QAction* toggleProblems = m_problemsDock->toggleViewAction();
    toggleProblems->setText(tr("Toggle &Problems"));
    toggleProblems->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    viewMenu->addAction(toggleProblems);
    addAction(toggleProblems);
    registerAction(toggleProblems);

    QAction* toggleBoard = m_boardDock->toggleViewAction();
    toggleBoard->setText(tr("Toggle Project &Board"));
    toggleBoard->setIcon(Icons::board());
    toggleBoard->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
    strip->addAction(toggleBoard);
    viewMenu->addAction(toggleBoard);
    addAction(toggleBoard);
    registerAction(toggleBoard);

    QAction* toggleTags = m_tagsDock->toggleViewAction();
    toggleTags->setText(tr("Toggle &Tags"));
    toggleTags->setIcon(Icons::tags());
    toggleTags->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    strip->addAction(toggleTags);
    viewMenu->addAction(toggleTags);
    addAction(toggleTags);
    registerAction(toggleTags);
    strip->addAction(palette);
    strip->addAction(toggleSidebar);
    addToolBar(Qt::LeftToolBarArea, strip);

    for (QAction* action : {openVault, newNoteAction, today, insertTemplateAction, insertNextIdAction, m_save, m_toggleReading, closeTab, quit, palette, toggleExplorer, toggleSidebar, m_verifyIndex, m_rebuildIndex, m_compactSidecar, m_gitignoreAction}) {
        addAction(action); // keeps shortcuts live when the menu bar is hidden
        registerAction(action);
    }
}

void MainWindow::createStatusBar()
{
    m_noteLabel = new QLabel;
    m_vaultLabel = new QLabel;
    m_indexLabel = new QLabel;
    statusBar()->addWidget(m_noteLabel, 1);
    m_problemsButton = new QToolButton;
    m_problemsButton->setAutoRaise(true);
    m_problemsButton->setToolTip(tr("Register problems in this project (Ctrl+Shift+M)"));
    m_problemsButton->hide();
    connect(m_problemsButton, &QToolButton::clicked, this, [this] {
        m_problemsDock->show();
        m_problemsDock->raise();
    });
    statusBar()->addPermanentWidget(m_problemsButton);
    m_gitButton = new QToolButton;
    m_gitButton->setAutoRaise(true);
    m_gitButton->hide();
    connect(m_gitButton, &QToolButton::clicked, this, &MainWindow::openCommitDialog);
    statusBar()->addPermanentWidget(m_gitButton);
    m_semanticLabel = new QLabel;
    m_semanticLabel->hide();
    statusBar()->addPermanentWidget(m_semanticLabel);
    statusBar()->addPermanentWidget(m_indexLabel);
    statusBar()->addPermanentWidget(m_vaultLabel);
    updateStatus();
}

void MainWindow::updateProblemsButton()
{
    if (!m_problemsButton)
        return;
    // Counts for the current note's project, whatever the panel's filters.
    Editor* editor = currentEditor();
    const auto project = editor ? m_catalogue->projectOf(editor->path()) : std::nullopt;
    if (!project) {
        m_problemsButton->hide();
        return;
    }
    int errors = 0;
    int warnings = 0;
    for (const ProblemRef& problem : m_catalogue->problems()) {
        const auto owner = m_catalogue->projectOf(problem.notePath);
        if (!owner || owner->rootPath != project->rootPath || owner->path != project->path)
            continue;
        errors += problem.level == QLatin1String("error");
        warnings += problem.level == QLatin1String("warning");
    }
    m_problemsButton->setText(QStringLiteral("✖ %1  ⚠ %2").arg(errors).arg(warnings));
    m_problemsButton->show();
}

void MainWindow::editShortcuts()
{
    auto* dialog = new ShortcutsDialog(m_actions, this);
    dialog->setObjectName(QStringLiteral("shortcutsDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this] { m_palette->setActions(m_actions); }); // shows the new keys
    dialog->open();
}

void MainWindow::registerAction(QAction* action)
{
    if (!action || m_actions.contains(action))
        return;
    Shortcuts::adopt(action); // the author's choice of shortcut, if any (F-027)
    m_actions.append(action);
    m_palette->setActions(m_actions);
}

void MainWindow::openVault(const Vault& vault)
{
    if (!confirmCloseAll())
        return;
    while (m_tabs->count() > 0) {
        if (NoteView* view = viewAt(0)) {
            m_journalled.remove(view->editor());
            m_conflictPrompts.remove(view->editor());
        }
        QWidget* page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        page->deleteLater();
    }
    m_vault = vault;
    m_linkedRoots.clear();
    if (!m_mcpLink->listen(vault.rootPath()))
        statusBar()->showMessage(tr("Another Coeus window is serving this vault to MCP clients"), kStatusTimeoutMs * 2);

    // The explorer lists linked roots at once; the index reconciles them on its thread.
    QList<ExplorerRoot> roots{ExplorerRoot{vault.name(), vault.rootPath(), true}};
    for (const RootConfig& root : Roots::load(vault.rootPath()).roots) {
        const auto opened = Vault::open(root.path);
        roots.append(ExplorerRoot{root.name, root.path, opened.has_value()});
        if (opened)
            m_linkedRoots.push_back({root.name, *opened});
    }
    m_explorer->setRoots(roots);
    setWindowTitle(QStringLiteral("%1 — Coeus").arg(vault.name()));
    updateStatus();
    sweepTempFiles(vault.rootPath());
    startIndex(vault.rootPath());
    m_gitignoreAction->setEnabled(true);
    checkGitignore(vault.rootPath(), true);
    checkRecovery(vault.rootPath());
}

void MainWindow::checkGitignore(const QString& vaultPath, bool onOpen)
{
    struct Check {
        QStringList missing;
        bool declined = false;
    };
    // Opening the repository is Git work: off the UI thread (AV-005).
    auto* watcher = new QFutureWatcher<Check>(this);
    connect(watcher, &QFutureWatcher<Check>::finished, this, [this, watcher, vaultPath, onOpen] {
        const Check check = watcher->result();
        watcher->deleteLater();
        if (!m_vault || m_vault->rootPath() != vaultPath)
            return; // another vault was opened meanwhile
        if (onOpen) {
            if (!check.missing.isEmpty() && !check.declined)
                offerGitignore(vaultPath, check.missing);
        } else if (check.missing.isEmpty()) {
            statusBar()->showMessage(tr("Nothing to add: the vault is not a Git repository, or already ignores Coeus's files."),
                                     kStatusTimeoutMs);
        } else {
            addToGitignore(vaultPath, check.missing); // the command itself is the author's request
        }
    });
    watcher->setFuture(QtConcurrent::run([vaultPath] {
        return Check{GitIgnoreOffer::missingPatterns(vaultPath), GitIgnoreOffer::declined(vaultPath)};
    }));
}

void MainWindow::offerGitignore(const QString& vaultPath, const QStringList& patterns)
{
    auto* box = new QMessageBox(QMessageBox::Question, tr("Keep Coeus files out of Git?"),
                                tr("This vault is a Git repository. Coeus keeps a rebuildable index in .coeus/ "
                                   "and can leave temporary files behind if it is interrupted mid-save.\n\n"
                                   "Add these lines to the vault's .gitignore?"),
                                QMessageBox::NoButton, this);
    box->setObjectName(QStringLiteral("gitignoreOffer"));
    box->setInformativeText(patterns.join(QLatin1Char('\n')));
    QPushButton* add = box->addButton(tr("Add to .gitignore"), QMessageBox::AcceptRole);
    add->setObjectName(QStringLiteral("add"));
    QPushButton* later = box->addButton(tr("Not Now"), QMessageBox::RejectRole);
    later->setObjectName(QStringLiteral("later"));
    QPushButton* never = box->addButton(tr("Don't Ask Again"), QMessageBox::DestructiveRole);
    never->setObjectName(QStringLiteral("never"));
    box->setDefaultButton(add);
    box->setAttribute(Qt::WA_DeleteOnClose);

    connect(box, &QMessageBox::buttonClicked, this, [this, vaultPath, patterns, add, never](QAbstractButton* button) {
        if (button == add) {
            addToGitignore(vaultPath, patterns);
        } else if (button == never) {
            if (const auto declined = GitIgnoreOffer::decline(vaultPath); !declined)
                statusBar()->showMessage(declined.error(), kStatusTimeoutMs * 2);
            else
                statusBar()->showMessage(tr("Coeus will not ask again. File › Add Coeus Files to .gitignore does it later."),
                                         kStatusTimeoutMs);
        }
    });
    box->open();
    emit gitignoreOffered(patterns);
}

void MainWindow::addToGitignore(const QString& vaultPath, const QStringList& patterns)
{
    auto* watcher = new QFutureWatcher<std::expected<void, QString>>(this);
    connect(watcher, &QFutureWatcher<std::expected<void, QString>>::finished, this, [this, watcher, patterns] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result) {
            statusBar()->showMessage(result.error(), kStatusTimeoutMs * 2);
            return;
        }
        statusBar()->showMessage(tr("Added to .gitignore: %1").arg(patterns.join(QStringLiteral(", "))), kStatusTimeoutMs);
        emit gitignoreUpdated(patterns);
    });
    watcher->setFuture(QtConcurrent::run([vaultPath, patterns] { return GitIgnoreOffer::accept(vaultPath, patterns); }));
}

MainWindow::~MainWindow()
{
    stopIndex();
}

void MainWindow::startIndex(const QString& vaultPath)
{
    stopIndex();
    m_indexThread = new QThread(this);
    m_indexThread->setObjectName(QStringLiteral("coeus-index"));
    m_index = new IndexService; // no parent: it moves to the worker thread
    m_index->moveToThread(m_indexThread);
    m_catalogue->setIndexPath(IndexService::indexPath(vaultPath));
    m_catalogue->setReviewThreshold(
        int(VaultConfig::integer(vaultPath, "registers", "review_threshold_days").value_or(Lint::kDefaultReviewThresholdDays)));
    m_backlinks->setIndexPath(IndexService::indexPath(vaultPath));
    m_tags->setIndexPath(IndexService::indexPath(vaultPath));
    m_search->setIndexPath(IndexService::indexPath(vaultPath));
    m_search->setModelPath(Embedder::modelPath(vaultPath));
    m_related->setIndexPath(IndexService::indexPath(vaultPath));
    connect(m_indexThread, &QThread::finished, m_index, &QObject::deleteLater);

    connect(m_index, &IndexService::busy, this, [this](const QString& what) { setIndexStatus(what); });
    connect(m_index, &IndexService::opened, this, [this](int notes, const QStringList& problems) {
        setIndexStatus(tr("index ok · %n note(s)", nullptr, notes), problems.join(QLatin1Char('\n')));
        if (!problems.isEmpty())
            statusBar()->showMessage(problems.front(), kStatusTimeoutMs * 2);
        m_verifyIndex->setEnabled(true);
        m_rebuildIndex->setEnabled(true);
        m_compactSidecar->setEnabled(true);
        m_catalogue->reload();
        m_backlinks->refresh();
        m_tags->reload();
        m_search->refresh();
        m_gitStatusTimer->start();
        emit indexOpened(notes);
    });
    connect(m_index, &IndexService::indexChanged, this, [this](int notes) {
        setIndexStatus(tr("index ok · %n note(s)", nullptr, notes), m_indexLabel->toolTip());
        m_catalogue->reload();
        m_backlinks->refresh();
        m_tags->reload();
        m_search->refresh();
        emit indexUpdated(notes);
    });
    connect(m_index, &IndexService::filesChanged, this, &MainWindow::onFilesChanged);
    connect(m_index, &IndexService::filesChanged, this, [this](const QStringList& paths) {
        if (paths.contains(m_history->notePath()))
            m_history->refresh(); // e.g. git checkout or pull changed it
        m_gitStatusTimer->start();
    });
    connect(m_index, &IndexService::rootRescanned, this, &MainWindow::onRootRescanned);
    connect(m_index, &IndexService::failed, this, [this](const QString& message) {
        setIndexStatus(tr("index error"), message);
        statusBar()->showMessage(message, kStatusTimeoutMs * 2);
    });
    connect(m_index, &IndexService::warning, this,
            [this](const QString& message) { statusBar()->showMessage(message, kStatusTimeoutMs * 2); });
    connect(m_index, &IndexService::verified, this, [this](const QString& summary, bool clean) {
        auto* box = new QMessageBox(clean ? QMessageBox::Information : QMessageBox::Warning, tr("Verify Index"),
                                    clean ? summary : tr("The index differs from the vault. Rebuild Index will repair it."),
                                    QMessageBox::Ok, this);
        if (!clean)
            box->setDetailedText(summary);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        setIndexStatus(m_indexLabel->text().startsWith(QLatin1String("index")) ? m_indexLabel->text()
                                                                               : tr("index ok"),
                       m_indexLabel->toolTip());
    });

    // The embedding worker (F-018): its own low-priority thread, woken by every index change.
    m_embedThread = new QThread(this);
    m_embedThread->setObjectName(QStringLiteral("coeus-embed"));
    m_embedding = new EmbeddingService; // no parent: it moves to the worker thread
    m_embedding->moveToThread(m_embedThread);
    connect(m_embedThread, &QThread::finished, m_embedding, &QObject::deleteLater);
    connect(m_embedding, &EmbeddingService::progress, this, [this](int embedded, int total) {
        m_semanticLabel->setVisible(total > 0);
        m_semanticLabel->setText(embedded >= total ? tr("meaning ✓") : tr("embedding %L1 of %L2").arg(embedded).arg(total));
        m_semanticLabel->setToolTip(tr("%L1 of %L2 passages have vectors for search by meaning (bge-small-en-v1.5)").arg(embedded).arg(total));
        m_relatedTimer->start(); // the note's new vectors may have landed (a save is re-embedded)
        emit embeddingProgress(embedded, total);
    });
    connect(m_embedding, &EmbeddingService::sidecarReport, this, [this](const QString& message) {
        statusBar()->showMessage(message, kStatusTimeoutMs * 2);
    });
    connect(m_embedding, &EmbeddingService::unavailable, this, [this](const QString& why) {
        m_semanticLabel->setVisible(true);
        m_semanticLabel->setText(tr("no embedding model"));
        m_semanticLabel->setToolTip(why);
    });
    const auto wakeEmbedding = [this, vaultPath](bool reopen) {
        QMetaObject::invokeMethod(m_embedding, [service = m_embedding, vaultPath, reopen] {
            if (reopen)
                service->open(vaultPath);
            service->wake();
        }, Qt::QueuedConnection);
    };
    connect(m_index, &IndexService::opened, this, [wakeEmbedding] { wakeEmbedding(true); });
    connect(m_index, &IndexService::indexChanged, this, [wakeEmbedding] { wakeEmbedding(false); });
    m_embedThread->start(QThread::LowPriority);

    m_indexThread->start();
    setIndexStatus(tr("Indexing…"));
    QMetaObject::invokeMethod(m_index, [service = m_index, vaultPath] { service->open(vaultPath); },
                              Qt::QueuedConnection);
}

void MainWindow::stopIndex()
{
    if (!m_indexThread)
        return;
    if (m_embedThread) {
        m_embedThread->quit(); // returns after the batch in hand, at most about half a second
        m_embedThread->wait();
        delete m_embedThread;
        m_embedThread = nullptr;
        m_embedding = nullptr;
        m_semanticLabel->hide();
    }
    m_indexThread->quit();
    m_indexThread->wait();
    delete m_indexThread;
    m_indexThread = nullptr;
    m_index = nullptr; // deleted on its own thread as it finished
    m_verifyIndex->setEnabled(false);
    m_rebuildIndex->setEnabled(false);
    m_compactSidecar->setEnabled(false);
}


void MainWindow::setIndexStatus(const QString& text, const QString& toolTip)
{
    m_indexLabel->setText(text);
    m_indexLabel->setToolTip(toolTip);
}

void MainWindow::sweepTempFiles(const QString& root)
{
    auto* watcher = new QFutureWatcher<TempSweeper::Result>(this);
    connect(watcher, &QFutureWatcher<TempSweeper::Result>::finished, this, [this, watcher] {
        const TempSweeper::Result result = watcher->result();
        watcher->deleteLater();
        if (result.removed > 0)
            statusBar()->showMessage(tr("Removed %n leftover temporary file(s) from interrupted saves.",
                                        nullptr, result.removed),
                                     kStatusTimeoutMs);
        emit tempSweepFinished(result.removed);
    });
    watcher->setFuture(QtConcurrent::run([root] { return TempSweeper::sweep(root); }));
}

void MainWindow::openNote(const QString& absolutePath)
{
    openEditor(absolutePath, false);
}

void MainWindow::openTodaysNote()
{
    if (!m_vault) {
        statusBar()->showMessage(tr("Open a vault first"), kStatusTimeoutMs);
        return;
    }
    // Creating it is an explicit command: through Vault::writeNote and AtomicWriter.
    const auto opened = DailyNotes::openOrCreate(*m_vault, QDateTime::currentDateTime());
    if (!opened) {
        statusBar()->showMessage(opened.error(), kStatusTimeoutMs * 2);
        return;
    }
    openNote(opened->path);
    if (!opened->unsupported.isEmpty())
        statusBar()->showMessage(tr("Left as written: %1 (daily notes take no register IDs)")
                                     .arg(opened->unsupported.join(QStringLiteral(", "))),
                                 kStatusTimeoutMs * 2);
    else if (opened->created)
        statusBar()->showMessage(tr("Created %1").arg(displayPath(opened->path)), kStatusTimeoutMs);
}

void MainWindow::chooseTemplate()
{
    if (!m_vault || !currentEditor()) {
        statusBar()->showMessage(tr("Open a note to insert a template into"), kStatusTimeoutMs);
        return;
    }
    const QStringList templates = Templates::list(m_vault->rootPath());
    if (templates.isEmpty()) {
        statusBar()->showMessage(tr("No templates: add .md files to %1/ (or set [templates] folder in config.toml)")
                                     .arg(Templates::folder(m_vault->rootPath())),
                                 kStatusTimeoutMs * 2);
        return;
    }
    delete m_templateActions; // the previous list's actions
    m_templateActions = new QObject(this);
    QList<QAction*> actions;
    const QString base = QDir::cleanPath(m_vault->rootPath() + QLatin1Char('/') + Templates::folder(m_vault->rootPath()));
    for (const QString& path : templates) {
        QString name = QDir(base).relativeFilePath(path);
        name.chop(3);
        auto* action = new QAction(name, m_templateActions);
        connect(action, &QAction::triggered, this, [this, path] { insertTemplate(path); });
        actions.append(action);
    }
    m_templatePicker->setActions(actions);
    m_templatePicker->popup();
}

void MainWindow::insertTemplate(const QString& templatePath)
{
    Editor* editor = currentEditor();
    if (!editor || !m_vault)
        return;
    const auto source = m_vault->readNote(templatePath);
    if (!source) {
        statusBar()->showMessage(source.error(), kStatusTimeoutMs);
        return;
    }
    const QStringList kinds = Templates::nextKinds(*source);
    const auto project = m_catalogue->projectOf(editor->path());
    if (kinds.isEmpty() || !project) {
        applyTemplate(editor, templatePath, *source, {});
        return;
    }

    // {{next:KIND}} takes a real ID, reserved like Insert Next ID (F-010, F-015).
    QPointer<Editor> target(editor);
    allocateTemplateIds(*project, kinds, [this, target, templatePath, text = *source](const QHash<QString, QString>& ids,
                                                                                     const QString& error) {
        if (!target)
            return;
        if (!error.isEmpty())
            statusBar()->showMessage(error, kStatusTimeoutMs * 2);
        applyTemplate(target, templatePath, text, ids);
    });
}

void MainWindow::allocateTemplateIds(const ProjectRef& project, const QStringList& kinds,
                                     std::function<void(const QHash<QString, QString>&, const QString&)> done)
{
    struct Allocated {
        QHash<QString, QString> ids;
        QString error;
    };
    QHash<QString, QString> unsaved;
    for (const QString& kind : kinds)
        unsaved.insert(kind, unsavedRegisterText(project, kind));
    auto* watcher = new QFutureWatcher<Allocated>(this);
    connect(watcher, &QFutureWatcher<Allocated>::finished, this, [watcher, done = std::move(done)] {
        const Allocated allocated = watcher->result();
        watcher->deleteLater();
        done(allocated.ids, allocated.error);
    });
    watcher->setFuture(QtConcurrent::run([index = IndexService::indexPath(m_vault->rootPath()), project, kinds, unsaved] {
        Allocated allocated;
        for (const QString& kind : kinds) {
            const auto id = NextId::allocate({index, project.rootPath, project.path, kind, QStringLiteral("gui"), unsaved.value(kind)});
            if (id)
                allocated.ids.insert(kind, id->ident);
            else
                allocated.error = id.error();
        }
        return allocated;
    }));
}

void MainWindow::newNote()
{
    if (!m_vault) {
        statusBar()->showMessage(tr("Open a vault first"), kStatusTimeoutMs);
        return;
    }
    QList<NewNoteDialog::Root> roots{{QFileInfo(m_vault->rootPath()).fileName(), m_vault->rootPath()}};
    for (const LinkedRoot& linked : m_linkedRoots)
        roots.append({linked.name, linked.vault.rootPath()});
    QList<NewNoteDialog::Template> templates;
    const QString base = QDir::cleanPath(m_vault->rootPath() + QLatin1Char('/') + Templates::folder(m_vault->rootPath()));
    for (const QString& path : Templates::list(m_vault->rootPath())) {
        QString name = QDir(base).relativeFilePath(path);
        name.chop(3);
        templates.append({name, path, Templates::placement(m_vault->readNote(path).value_or(QString()))});
    }
    // Where to start: the explorer's folder, else the current note's, else the vault.
    QString folder = m_explorer->selectedFolder();
    if (folder.isEmpty() && currentView())
        folder = QFileInfo(currentView()->path()).absolutePath();
    int rootIndex = 0;
    QString relative;
    qsizetype longest = -1;
    for (qsizetype i = 0; i < roots.size(); ++i) {
        const QString& rootPath = roots[i].path;
        if ((folder == rootPath || folder.startsWith(rootPath + QLatin1Char('/'))) && rootPath.size() > longest) {
            longest = rootPath.size();
            rootIndex = int(i);
            relative = folder == rootPath ? QString() : QDir(rootPath).relativeFilePath(folder);
        }
    }
    auto* dialog = new NewNoteDialog(roots, templates, rootIndex, relative, this);
    dialog->setObjectName(QStringLiteral("newNoteDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] { createNote(dialog->notePath(), dialog->templatePath()); });
    dialog->open();
}

void MainWindow::createNote(const QString& absolutePath, const QString& templatePath)
{
    const Vault* root = m_vault ? rootFor(QFileInfo(absolutePath).absolutePath()) : nullptr;
    if (!root && m_vault) {
        // A folder not made yet: the deepest root whose path contains it.
        QList<const Vault*> candidates{&*m_vault};
        for (const LinkedRoot& linked : m_linkedRoots)
            candidates.append(&linked.vault);
        for (const Vault* candidate : std::as_const(candidates))
            if (absolutePath.startsWith(candidate->rootPath() + QLatin1Char('/'))
                && (!root || candidate->rootPath().size() > root->rootPath().size()))
                root = candidate;
    }
    if (!root) {
        statusBar()->showMessage(tr("Outside the vault and its linked roots: %1").arg(absolutePath), kStatusTimeoutMs * 2);
        return;
    }
    QString source;
    if (!templatePath.isEmpty()) {
        const auto read = m_vault->readNote(templatePath);
        if (!read) {
            statusBar()->showMessage(read.error(), kStatusTimeoutMs * 2);
            return;
        }
        source = *read;
    }
    const auto write = [this, root, absolutePath, templatePath, source](const QHash<QString, QString>& ids, const QString& error) {
        if (QFileInfo::exists(absolutePath)) { // appeared meanwhile: never overwritten
            statusBar()->showMessage(tr("%1 already exists").arg(displayPath(absolutePath)), kStatusTimeoutMs * 2);
            return;
        }
        Templates::Rendered rendered{NewNote::blank(absolutePath), {}};
        if (!templatePath.isEmpty())
            rendered = Templates::render(source, QDateTime::currentDateTime(), QFileInfo(absolutePath).completeBaseName(), ids);
        if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
            statusBar()->showMessage(tr("Cannot create %1").arg(QFileInfo(absolutePath).absolutePath()), kStatusTimeoutMs * 2);
            return;
        }
        // An explicit user command, through Vault::writeNote and AtomicWriter.
        if (auto written = root->writeNote(absolutePath, rendered.text.toUtf8()); !written) {
            statusBar()->showMessage(written.error(), kStatusTimeoutMs * 2);
            return;
        }
        openNote(absolutePath);
        if (NoteView* view = currentView())
            view->setMode(NoteView::Mode::Source);
        if (!error.isEmpty())
            statusBar()->showMessage(error, kStatusTimeoutMs * 2);
        else if (!rendered.unsupported.isEmpty())
            statusBar()->showMessage(tr("Created %1; left as written: %2 ({{next:…}} needs a note inside a project)")
                                         .arg(displayPath(absolutePath), rendered.unsupported.join(QStringLiteral(", "))),
                                     kStatusTimeoutMs * 2);
        else
            statusBar()->showMessage(tr("Created %1").arg(displayPath(absolutePath)), kStatusTimeoutMs);
        emit noteCreated(absolutePath);
    };
    const QStringList kinds = Templates::nextKinds(source);
    const auto project = m_catalogue->projectOf(absolutePath);
    if (kinds.isEmpty() || !project)
        write({}, {});
    else
        allocateTemplateIds(*project, kinds, write);
}

void MainWindow::applyTemplate(Editor* editor, const QString& templatePath, const QString& source,
                               const QHash<QString, QString>& nextIds)
{
    const Templates::Rendered rendered = Templates::render(source, QDateTime::currentDateTime(),
                                                           QFileInfo(editor->path()).completeBaseName(), nextIds);
    const QString before = editor->toPlainText();
    int caret = 0;
    const QString after = Templates::insert(before, editor->textCursor().position(), rendered.text, &caret);

    // Replace only the span that changed, as one undo step.
    int prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix])
        ++prefix;
    int suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(prefix);
    cursor.setPosition(int(before.size()) - suffix, QTextCursor::KeepAnchor);
    cursor.insertText(after.mid(prefix, after.size() - prefix - suffix));
    cursor.endEditBlock();
    cursor.setPosition(std::min(caret, int(editor->document()->characterCount()) - 1));
    editor->setTextCursor(cursor);
    if (NoteView* view = currentView())
        view->setMode(NoteView::Mode::Source);
    editor->setFocus();
    if (!rendered.unsupported.isEmpty())
        statusBar()->showMessage(tr("Left as written: %1 ({{next:…}} needs a note inside a project)")
                                     .arg(rendered.unsupported.join(QStringLiteral(", "))),
                                 kStatusTimeoutMs * 2);
    else if (!nextIds.isEmpty())
        statusBar()->showMessage(tr("Reserved %1 for %2 s; save to keep them")
                                     .arg(QStringList(nextIds.values()).join(QStringLiteral(", ")))
                                     .arg(NextId::kReservationSeconds),
                                 kStatusTimeoutMs * 2);
    emit templateInserted(templatePath);
}

QString MainWindow::unsavedRegisterText(const ProjectRef& project, const QString& kind) const
{
    const QString fileName = RegisterTemplates::fileName(kind);
    QString text;
    for (int i = 0; i < m_tabs->count(); ++i) {
        Editor* editor = editorAt(i);
        if (!editor || !editor->isModified() || QFileInfo(editor->path()).fileName() != fileName)
            continue;
        const auto owner = m_catalogue->projectOf(editor->path());
        if (owner && owner->rootPath == project.rootPath && owner->path == project.path)
            text += editor->toPlainText() + QLatin1Char('\n');
    }
    return text;
}

void MainWindow::chooseNextId()
{
    Editor* editor = currentEditor();
    const auto project = editor ? m_catalogue->projectOf(editor->path()) : std::nullopt;
    if (!project) {
        statusBar()->showMessage(tr("Open a note inside a project (a folder whose README opens with the status header)"),
                                 kStatusTimeoutMs * 2);
        return;
    }
    delete m_nextIdActions;
    m_nextIdActions = new QObject(this);
    // The current register file's kind first.
    QStringList kinds = RegisterTemplates::kinds();
    const QString here = RegisterParser::kindOfFile(QFileInfo(editor->path()).fileName());
    if (!here.isEmpty()) {
        kinds.removeAll(here);
        kinds.prepend(here);
    }
    QList<QAction*> actions;
    for (const QString& kind : std::as_const(kinds)) {
        auto* action = new QAction(QStringLiteral("%1 — %2").arg(kind, RegisterTemplates::displayName(kind)), m_nextIdActions);
        connect(action, &QAction::triggered, this, [this, kind] { insertNextId(kind); });
        actions.append(action);
    }
    m_nextIdPicker->setActions(actions);
    m_nextIdPicker->popup();
}

void MainWindow::insertNextId(const QString& kind)
{
    Editor* editor = currentEditor();
    const auto project = editor ? m_catalogue->projectOf(editor->path()) : std::nullopt;
    if (!project || !m_vault)
        return;
    auto* watcher = new QFutureWatcher<std::expected<NextId::Allocation, QString>>(this);
    connect(watcher, &QFutureWatcher<std::expected<NextId::Allocation, QString>>::finished, this, [this, watcher, kind] {
        const auto allocation = watcher->result();
        watcher->deleteLater();
        if (!allocation) {
            statusBar()->showMessage(allocation.error(), kStatusTimeoutMs * 2);
            return;
        }
        // A missing register file is created from the standard's skeleton:
        // an explicit command, through Vault::writeNote and AtomicWriter.
        if (!QFileInfo::exists(allocation->registerFile)) {
            const Vault* root = rootFor(QFileInfo(allocation->registerFile).absolutePath());
            if (!root) {
                statusBar()->showMessage(tr("Outside the vault and its linked roots: %1").arg(allocation->registerFile), kStatusTimeoutMs);
                return;
            }
            if (auto written = root->writeNote(allocation->registerFile, RegisterTemplates::fileSkeleton(kind).toUtf8()); !written) {
                statusBar()->showMessage(written.error(), kStatusTimeoutMs * 2);
                return;
            }
        }
        Editor* target = openEditor(allocation->registerFile, false);
        if (!target)
            return;
        if (NoteView* view = currentView())
            view->setMode(NoteView::Mode::Source);

        // Append the entry, a blank line apart, and select its title to type over.
        const QString text = target->toPlainText();
        QString separator;
        if (!text.isEmpty() && !text.endsWith(QStringLiteral("\n\n")))
            separator = text.endsWith(QLatin1Char('\n')) ? QStringLiteral("\n") : QStringLiteral("\n\n");
        const QString entry = RegisterTemplates::entry(kind, allocation->ident, QDate::currentDate());
        QTextCursor cursor(target->document());
        cursor.movePosition(QTextCursor::End);
        cursor.beginEditBlock();
        cursor.insertText(separator + entry);
        cursor.endEditBlock();
        const int titleAt = int(text.size() + separator.size() + entry.indexOf(QLatin1String(RegisterTemplates::kTitle)));
        cursor.setPosition(titleAt);
        cursor.setPosition(titleAt + int(qstrlen(RegisterTemplates::kTitle)), QTextCursor::KeepAnchor);
        target->setTextCursor(cursor);
        target->centerCursor();
        target->setFocus();
        statusBar()->showMessage(tr("%1 reserved for %2 s; save the register to keep it")
                                     .arg(allocation->ident)
                                     .arg(NextId::kReservationSeconds),
                                 kStatusTimeoutMs * 2);
        emit nextIdInserted(allocation->ident, allocation->registerFile);
    });
    watcher->setFuture(QtConcurrent::run([request = NextId::Request{IndexService::indexPath(m_vault->rootPath()), project->rootPath,
                                                                    project->path, kind, QStringLiteral("gui"),
                                                                    unsavedRegisterText(*project, kind)}] {
        return NextId::allocate(request);
    }));
}

void MainWindow::openNoteAtWords(const QString& absolutePath, const QStringList& words)
{
    Editor* editor = openEditor(absolutePath, false);
    if (!editor)
        return;
    for (const QString& word : words) {
        const QTextCursor found = editor->document()->find(word);
        if (!found.isNull()) {
            editor->setTextCursor(found);
            editor->centerCursor();
            break;
        }
    }
    editor->setFocus();
}

void MainWindow::openNoteAt(const QString& absolutePath, int line)
{
    Editor* editor = openEditor(absolutePath, false);
    if (!editor || line < 1)
        return;
    const QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
}

Editor* MainWindow::openEditor(const QString& absolutePath, bool allowMissing)
{
    if (!m_vault)
        return nullptr;

    if (const int existing = tabIndexFor(absolutePath); existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return editorAt(existing);
    }

    const Vault* root = rootFor(absolutePath);
    if (!root && allowMissing) {
        // A note deleted since: its folder decides the root.
        for (const Vault* candidate : {&*m_vault}) {
            if (candidate->contains(QFileInfo(absolutePath).absolutePath()))
                root = candidate;
        }
        for (const LinkedRoot& linked : m_linkedRoots) {
            if (!root && linked.vault.contains(QFileInfo(absolutePath).absolutePath()))
                root = &linked.vault;
        }
    }
    if (!root) {
        statusBar()->showMessage(tr("Outside the vault and its linked roots: %1").arg(absolutePath), kStatusTimeoutMs);
        return nullptr;
    }

    if (!Vault::isNote(absolutePath)) {
        statusBar()->showMessage(tr("Attachments cannot be previewed yet: %1").arg(displayPath(absolutePath)),
                                 kStatusTimeoutMs);
        return nullptr;
    }

    auto* view = new NoteView(*root, absolutePath, displayPath(absolutePath));
    Editor* editor = view->editor();
    if (QFileInfo::exists(absolutePath) || !allowMissing) {
        if (const auto loaded = editor->load(); !loaded) {
            statusBar()->showMessage(loaded.error(), kStatusTimeoutMs);
            delete view;
            return nullptr;
        }
    }
    view->setProperty(kPathProperty, absolutePath);
    editor->setCatalogue(m_catalogue);
    connect(editor, &Editor::blameRequested, this, [this, editor] { requestBlame(editor); });
    connect(editor, &Editor::blameShownChanged, this, [this, editor](bool shown) {
        if (editor == currentEditor())
            m_history->setBlameChecked(shown);
    });
    connect(editor, &Editor::modificationChanged, this, [this, editor] { updateTabTitle(editor); });
    connect(editor, &Editor::conflictChanged, this, [this, editor] { updateTabTitle(editor); });
    connect(view, &NoteView::linkActivated, this, [this, view](const QUrl& url) { followLink(view, url); });

    const int index = m_tabs->addTab(view, QFileInfo(absolutePath).completeBaseName());
    m_tabs->setTabToolTip(index, displayPath(absolutePath));
    m_tabs->setCurrentIndex(index);
    return editor;
}

NoteView* MainWindow::viewAt(int index) const
{
    return qobject_cast<NoteView*>(m_tabs->widget(index));
}

NoteView* MainWindow::currentView() const
{
    return qobject_cast<NoteView*>(m_tabs->currentWidget());
}

Editor* MainWindow::editorAt(int index) const
{
    NoteView* view = viewAt(index);
    return view ? view->editor() : nullptr;
}

Editor* MainWindow::currentEditor() const
{
    NoteView* view = currentView();
    return view ? view->editor() : nullptr;
}

NoteView* MainWindow::viewOf(const Editor* editor) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (NoteView* view = viewAt(i); view && view->editor() == editor)
            return view;
    }
    return nullptr;
}

void MainWindow::toggleReadingMode()
{
    if (NoteView* view = currentView())
        view->toggleMode();
}

void MainWindow::followLink(NoteView* view, const QUrl& url)
{
    const QString scheme = url.scheme();
    if (scheme == QLatin1String("coeus-tag")) {
        showTag(url.path());
        return;
    }
    if (scheme == QLatin1String("coeus-file")) {
        // A Markdown link: a path relative to the note (SPEC.md §Wikilink resolution).
        const QString relative = url.path();
        QString target = relative.startsWith(QLatin1Char('/'))
                             ? QDir::cleanPath(rootFor(view->path())->rootPath() + relative)
                             : QDir::cleanPath(QFileInfo(view->path()).absolutePath() + QLatin1Char('/') + relative);
        if (!QFileInfo(target).isFile() && QFileInfo(target + QStringLiteral(".md")).isFile())
            target += QStringLiteral(".md");
        if (!QFileInfo(target).isFile()) {
            statusBar()->showMessage(tr("No such file: %1").arg(relative), kStatusTimeoutMs);
            return;
        }
        openNote(target);
        return;
    }
    if (scheme == QLatin1String("coeus-wiki")) {
        // As the index resolved it; the index never guesses (AV-006).
        QString target = url.path();
        if (const qsizetype caret = target.indexOf(QLatin1Char('^')); caret >= 0)
            target.truncate(caret);
        const auto result = IndexQueries::wikilinkTarget(IndexService::indexPath(m_vault->rootPath()), view->path(), target);
        if (!result) {
            statusBar()->showMessage(result.error(), kStatusTimeoutMs);
        } else if (!*result) {
            statusBar()->showMessage(tr("[[%1]] is not indexed yet; save the note and try again").arg(target), kStatusTimeoutMs);
        } else if ((*result)->ambiguous) {
            statusBar()->showMessage(tr("[[%1]] matches more than one note; use a path to choose").arg(target), kStatusTimeoutMs * 2);
        } else if ((*result)->absolutePath.isEmpty()) {
            statusBar()->showMessage(tr("No note matches [[%1]]").arg(target), kStatusTimeoutMs);
        } else {
            openNote((*result)->absolutePath);
        }
        return;
    }
    if (url.isRelative() && url.hasFragment())
        return; // an in-page anchor
    QDesktopServices::openUrl(url); // external: the desktop decides
}

Editor* MainWindow::editorFor(const QString& absolutePath) const
{
    const int index = tabIndexFor(absolutePath);
    return index >= 0 ? editorAt(index) : nullptr;
}

void MainWindow::updateTabTitle(Editor* editor)
{
    const int index = m_tabs->indexOf(viewOf(editor));
    if (index < 0)
        return;
    QString title = QFileInfo(editor->path()).completeBaseName();
    if (editor->inConflict())
        title = QStringLiteral("⚠ ") + title;
    else if (editor->isModified())
        title = QStringLiteral("● ") + title;
    m_tabs->setTabText(index, title);
}

bool MainWindow::saveCurrent()
{
    Editor* editor = currentEditor();
    return editor && saveEditor(editor);
}

bool MainWindow::saveEditor(Editor* editor)
{
    QString error;
    switch (editor->save(&error)) {
    case Editor::SaveResult::Saved:
        forgetJournal(editor->path());
        statusBar()->showMessage(tr("Saved %1").arg(displayPath(editor->path())), kStatusTimeoutMs);
        return true;
    case Editor::SaveResult::Conflict:
        promptConflict(editor, true);
        return false;
    case Editor::SaveResult::Failed:
        statusBar()->showMessage(error, kStatusTimeoutMs * 2);
        return false;
    }
    return false;
}

void MainWindow::promptConflict(Editor* editor, bool retrySave)
{
    if (m_conflictPrompts.value(editor))
        return; // already asking
    if (NoteView* view = viewOf(editor))
        m_tabs->setCurrentWidget(view);

    auto* box = new QMessageBox(QMessageBox::Warning, tr("Note changed on disk"),
                                tr("%1 was changed outside Coeus while you have unsaved changes.")
                                    .arg(displayPath(editor->path())),
                                QMessageBox::NoButton, this);
    box->setObjectName(QStringLiteral("conflictPrompt"));
    box->setInformativeText(tr("Keep your version to overwrite the file on the next save, or load the "
                               "version on disk and discard your changes."));
    QPushButton* mine = box->addButton(tr("Keep My Version"), QMessageBox::AcceptRole);
    mine->setObjectName(QStringLiteral("mine"));
    QPushButton* disk = box->addButton(tr("Load Disk Version"), QMessageBox::DestructiveRole);
    disk->setObjectName(QStringLiteral("disk"));
    QPushButton* later = box->addButton(tr("Decide Later"), QMessageBox::RejectRole);
    later->setObjectName(QStringLiteral("later"));
    box->setDefaultButton(later);
    box->setAttribute(Qt::WA_DeleteOnClose);
    m_conflictPrompts.insert(editor, box);

    QPointer<Editor> guarded(editor);
    connect(box, &QMessageBox::buttonClicked, this, [this, guarded, mine, disk, retrySave](QAbstractButton* button) {
        if (!guarded)
            return;
        if (button == mine) {
            guarded->keepMine();
            if (retrySave)
                saveEditor(guarded);
        } else if (button == disk) {
            if (const auto loaded = guarded->takeDisk(); !loaded)
                statusBar()->showMessage(loaded.error(), kStatusTimeoutMs * 2);
            else
                forgetJournal(guarded->path());
        }
    });
    box->open();
    emit conflictPrompted(editor->path());
}

void MainWindow::handleDiskChange(Editor* editor)
{
    switch (editor->checkDisk()) {
    case Editor::DiskState::Unchanged:
        break;
    case Editor::DiskState::Reloaded:
        statusBar()->showMessage(tr("Reloaded %1: it changed on disk").arg(displayPath(editor->path())), kStatusTimeoutMs);
        break;
    case Editor::DiskState::Conflict:
        promptConflict(editor, false);
        break;
    case Editor::DiskState::Deleted:
        statusBar()->showMessage(tr("%1 was deleted on disk; saving will recreate it").arg(displayPath(editor->path())),
                                 kStatusTimeoutMs * 2);
        updateTabTitle(editor);
        break;
    }
}

void MainWindow::onFilesChanged(const QStringList& absolutePaths)
{
    for (const QString& path : absolutePaths) {
        if (Editor* editor = editorFor(path))
            handleDiskChange(editor);
    }
}

void MainWindow::onRootRescanned(const QString& rootPath)
{
    const QString prefix = rootPath + QLatin1Char('/');
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (Editor* editor = editorAt(i); editor && editor->path().startsWith(prefix))
            handleDiskChange(editor);
    }
}

void MainWindow::journalNow()
{
    if (!m_vault)
        return;
    const QString vaultPath = m_vault->rootPath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        Editor* editor = editorAt(i);
        if (!editor || !editor->isModified())
            continue;
        const int revision = editor->document()->revision();
        if (m_journalled.value(editor, -1) == revision)
            continue;
        m_journalled.insert(editor, revision);
        m_journalPool->start([vaultPath, path = editor->path(), base = editor->baseHash(), text = editor->toPlainText()] {
            (void)Journal::write(vaultPath, path, base, text);
        });
    }
}

void MainWindow::waitForJournal()
{
    m_journalPool->waitForDone();
}

void MainWindow::forgetJournal(const QString& notePath)
{
    if (!m_vault)
        return;
    if (Editor* editor = editorFor(notePath))
        m_journalled.remove(editor);
    m_journalPool->start([vaultPath = m_vault->rootPath(), notePath] { Journal::remove(vaultPath, notePath); });
}

void MainWindow::checkRecovery(const QString& vaultPath)
{
    auto* watcher = new QFutureWatcher<QList<Journal::Entry>>(this);
    connect(watcher, &QFutureWatcher<QList<Journal::Entry>>::finished, this, [this, watcher, vaultPath] {
        const QList<Journal::Entry> entries = watcher->result();
        watcher->deleteLater();
        if (entries.isEmpty() || !m_vault || m_vault->rootPath() != vaultPath)
            return;

        QStringList paths;
        QStringList lines;
        for (const Journal::Entry& entry : entries) {
            paths.append(entry.notePath);
            QString line = displayPath(entry.notePath);
            if (!QFileInfo::exists(entry.notePath))
                line += tr(" (deleted since)");
            else if (entry.noteChangedSince)
                line += tr(" (changed on disk since)");
            lines.append(line);
        }
        auto* box = new QMessageBox(QMessageBox::Question, tr("Recover unsaved changes?"),
                                    tr("Coeus closed with unsaved changes in %n note(s).", nullptr, int(entries.size())),
                                    QMessageBox::NoButton, this);
        box->setObjectName(QStringLiteral("recoveryPrompt"));
        box->setInformativeText(tr("Recovered text opens as unsaved changes; nothing is written until you save.")
                                + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n')));
        QPushButton* recover = box->addButton(tr("Recover"), QMessageBox::AcceptRole);
        recover->setObjectName(QStringLiteral("recover"));
        QPushButton* discard = box->addButton(tr("Discard"), QMessageBox::DestructiveRole);
        discard->setObjectName(QStringLiteral("discard"));
        QPushButton* later = box->addButton(tr("Later"), QMessageBox::RejectRole);
        later->setObjectName(QStringLiteral("later"));
        box->setDefaultButton(recover);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QMessageBox::buttonClicked, this, [this, entries, recover, discard](QAbstractButton* button) {
            if (button == recover) {
                for (const Journal::Entry& entry : entries) {
                    if (Editor* editor = openEditor(entry.notePath, true))
                        editor->restore(entry.text, entry.baseHash);
                }
            } else if (button == discard) {
                for (const Journal::Entry& entry : entries)
                    QFile::remove(entry.file);
            }
        });
        box->open();
        emit recoveryOffered(paths);
    });
    watcher->setFuture(QtConcurrent::run([vaultPath] { return Journal::recoverable(vaultPath); }));
}

const Vault* MainWindow::rootFor(const QString& absolutePath) const
{
    if (m_vault && m_vault->contains(absolutePath))
        return &*m_vault;
    for (const LinkedRoot& root : m_linkedRoots) {
        if (root.vault.contains(absolutePath))
            return &root.vault;
    }
    return nullptr;
}

QString MainWindow::displayPath(const QString& absolutePath) const
{
    const Vault* root = rootFor(absolutePath);
    if (!root)
        return absolutePath;
    if (root == &*m_vault)
        return root->relativePath(absolutePath);
    for (const LinkedRoot& linked : m_linkedRoots) {
        if (&linked.vault == root)
            return linked.name + QLatin1Char('/') + root->relativePath(absolutePath);
    }
    return absolutePath;
}

int MainWindow::tabIndexFor(const QString& absolutePath) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property(kPathProperty).toString() == absolutePath)
            return i;
    }
    return -1;
}

void MainWindow::chooseVault()
{
    const QString start = m_vault ? m_vault->rootPath() : QString();
    const QString path = QFileDialog::getExistingDirectory(this, tr("Open Vault"), start);
    if (path.isEmpty())
        return;
    const auto vault = Vault::open(path);
    if (!vault) {
        statusBar()->showMessage(vault.error(), kStatusTimeoutMs);
        return;
    }
    openVault(*vault);
}

void MainWindow::closeCurrentTab()
{
    const int index = m_tabs->currentIndex();
    if (index >= 0)
        closeTab(index);
}

bool MainWindow::closeTab(int index)
{
    Editor* editor = editorAt(index);
    if (!editor) {
        // A read-only page (a VersionView): nothing to save.
        QWidget* page = m_tabs->widget(index);
        m_tabs->removeTab(index);
        page->deleteLater();
        return true;
    }
    if (editor->isModified()) {
        QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                        tr("Save changes to %1?").arg(displayPath(editor->path())),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.setObjectName(QStringLiteral("closePrompt"));
        box.setDefaultButton(QMessageBox::Save);
        const int answer = box.exec();
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save && !saveEditor(editor))
            return false;
        if (answer == QMessageBox::Discard)
            forgetJournal(editor->path());
    }
    m_journalled.remove(editor);
    m_conflictPrompts.remove(editor);
    NoteView* view = viewOf(editor); // the index may have moved while the prompt was open
    m_tabs->removeTab(m_tabs->indexOf(view));
    view->deleteLater();
    return true;
}

bool MainWindow::confirmCloseAll()
{
    QList<Editor*> modified;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (Editor* editor = editorAt(i); editor && editor->isModified())
            modified.append(editor);
    }
    if (modified.isEmpty())
        return true;

    QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                    tr("%n note(s) have unsaved changes.", nullptr, int(modified.size())),
                    QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("closeAllPrompt"));
    box.setDefaultButton(QMessageBox::SaveAll);
    const int answer = box.exec();
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::SaveAll) {
        bool all = true;
        for (Editor* editor : std::as_const(modified))
            all = saveEditor(editor) && all;
        return all;
    }
    for (Editor* editor : std::as_const(modified)) {
        forgetJournal(editor->path());
        editor->document()->setModified(false);
    }
    return true;
}

void MainWindow::showCommandPalette()
{
    m_palette->popup();
}

void MainWindow::updateStatus()
{
    if (!m_vault) {
        m_noteLabel->setText(tr("No vault open — File › Open Vault…"));
        m_vaultLabel->clear();
        return;
    }
    m_vaultLabel->setText(m_vault->rootPath());

    QWidget* page = m_tabs->currentWidget();
    if (const auto* version = qobject_cast<VersionView*>(page)) {
        m_noteLabel->setText(tr("%1 as of %2 (read-only)").arg(displayPath(version->notePath()), version->commit().shortId));
        return;
    }
    if (const auto* diff = qobject_cast<DiffView*>(page)) {
        m_noteLabel->setText(diff->summary());
        return;
    }
    m_noteLabel->setText(page ? displayPath(page->property(kPathProperty).toString()) : QString());
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray(), kLayoutVersion);
}

void MainWindow::saveLayout() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState(kLayoutVersion));
}

void MainWindow::openVersion(const QString& notePath, const GitCommit& commit, const QString& workdir)
{
    const QString key = notePath + QLatin1Char('@') + commit.id;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property(kVersionProperty).toString() == key) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    // Read the blob on the Git worker, then open the tab.
    auto* watcher = new QFutureWatcher<std::expected<QByteArray, QString>>(this);
    connect(watcher, &QFutureWatcher<std::expected<QByteArray, QString>>::finished, this,
            [this, watcher, notePath, commit, key] {
                watcher->deleteLater();
                const auto bytes = watcher->result();
                if (!bytes) {
                    statusBar()->showMessage(bytes.error(), kStatusTimeoutMs * 2);
                    return;
                }
                auto* view = new VersionView(notePath, commit, QString::fromUtf8(*bytes));
                view->setProperty(kVersionProperty, key);
                const int index = m_tabs->addTab(view, view->title());
                m_tabs->setTabToolTip(index, tr("%1 as of %2 — %3").arg(displayPath(notePath), commit.shortId, commit.summary));
                m_tabs->setCurrentIndex(index);
                emit versionOpened(view);
            });
    watcher->setFuture(QtConcurrent::run(m_gitPool, [workdir, commit]() -> std::expected<QByteArray, QString> {
        const auto repo = GitRepository::discover(workdir);
        if (!repo)
            return std::unexpected(repo.error());
        return repo->fileAt(commit.id, commit.path);
    }));
}

void MainWindow::diffWithHead(const QString& notePath)
{
    if (notePath.isEmpty())
        return;
    openDiff(notePath, {DiffSide::Kind::Revision, QStringLiteral("HEAD"), QString(), QStringLiteral("HEAD")},
             {DiffSide::Kind::WorkingCopy, QString(), QString(), tr("working copy")});
}

void MainWindow::openDiff(const QString& notePath, const DiffSide& older, const DiffSide& newer)
{
    const auto keyOf = [](const DiffSide& side) {
        return side.kind == DiffSide::Kind::Revision ? side.revision + QLatin1Char(':') + side.path
                                                     : QString::number(int(side.kind));
    };
    const QString key = notePath + QLatin1Char('|') + keyOf(older) + QLatin1Char('|') + keyOf(newer);
    const bool live = older.kind == DiffSide::Kind::WorkingCopy || newer.kind == DiffSide::Kind::WorkingCopy
                      || older.revision == QLatin1String("HEAD");
    int replace = -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property(kDiffProperty).toString() != key)
            continue;
        if (!live) {
            m_tabs->setCurrentIndex(i); // two commits never change
            return;
        }
        replace = i; // the working copy may have: compare again in its place
    }

    // The working copy as the author sees it: the open buffer, unsaved changes included.
    const Editor* editor = editorFor(notePath);
    const bool fromBuffer = editor != nullptr;
    const QByteArray buffer = editor ? editor->toPlainText().toUtf8() : QByteArray();
    using Result = std::expected<QList<DiffLine>, QString>;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, notePath, older, newer, key, replace] {
        watcher->deleteLater();
        const Result lines = watcher->result();
        if (!lines) {
            statusBar()->showMessage(lines.error(), kStatusTimeoutMs * 2);
            return;
        }
        auto* view = new DiffView(notePath, older.label, newer.label, *lines);
        view->setProperty(kDiffProperty, key);
        int index = -1;
        if (replace >= 0 && replace < m_tabs->count() && m_tabs->widget(replace)->property(kDiffProperty).toString() == key) {
            QWidget* old = m_tabs->widget(replace);
            m_tabs->removeTab(replace);
            old->deleteLater();
            index = m_tabs->insertTab(replace, view, view->title());
        } else {
            index = m_tabs->addTab(view, view->title());
        }
        m_tabs->setTabToolTip(index, view->summary());
        m_tabs->setCurrentIndex(index);
        emit diffOpened(view);
    });
    watcher->setFuture(QtConcurrent::run(m_gitPool, [notePath, older, newer, fromBuffer, buffer]() -> Result {
        const auto repo = GitRepository::discover(notePath);
        if (!repo)
            return std::unexpected(repo.error());
        const auto text = [&](const DiffSide& side) -> std::expected<QByteArray, QString> {
            switch (side.kind) {
            case DiffSide::Kind::Nothing: return QByteArray();
            case DiffSide::Kind::Revision: {
                const QString path = side.path.isEmpty() ? repo->relativePath(notePath) : side.path;
                const auto bytes = repo->fileAt(side.revision, path);
                if (!bytes && side.revision == QLatin1String("HEAD"))
                    return QByteArray(); // not in HEAD yet: all of it is new
                return bytes;
            }
            case DiffSide::Kind::WorkingCopy: {
                if (fromBuffer)
                    return buffer;
                QFile file(notePath);
                if (!file.open(QIODevice::ReadOnly))
                    return std::unexpected(QStringLiteral("Cannot read %1").arg(notePath));
                return file.readAll();
            }
            }
            return QByteArray();
        };
        const auto before = text(older);
        if (!before)
            return std::unexpected(before.error());
        const auto after = text(newer);
        if (!after)
            return std::unexpected(after.error());
        return GitRepository::diff(*before, *after);
    }));
}

void MainWindow::setBlameShown(bool shown)
{
    Editor* editor = currentEditor();
    if (!editor) {
        m_history->setBlameChecked(false);
        return;
    }
    if (shown)
        editor->showBlame(); // asks for blame through blameRequested
    else
        editor->hideBlame();
}

void MainWindow::requestBlame(Editor* editor)
{
    // Newest request wins: a slower, older blame never overwrites a newer one.
    const int generation = editor->property("coeusBlameGeneration").toInt() + 1;
    editor->setProperty("coeusBlameGeneration", generation);
    using Result = std::expected<QList<BlameLine>, QString>;
    auto* watcher = new QFutureWatcher<Result>(this);
    QPointer<Editor> target(editor);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, target, generation] {
        watcher->deleteLater();
        if (!target || target->property("coeusBlameGeneration").toInt() != generation || !target->blameShown())
            return;
        const Result lines = watcher->result();
        if (!lines) {
            target->hideBlame();
            statusBar()->showMessage(lines.error(), kStatusTimeoutMs * 2);
            return;
        }
        target->setBlame(*lines);
        emit blameApplied(target);
    });
    watcher->setFuture(QtConcurrent::run(m_gitPool, [path = editor->path(), buffer = editor->toPlainText().toUtf8()]() -> Result {
        const auto repo = GitRepository::discover(path);
        if (!repo)
            return std::unexpected(repo.error());
        return repo->blame(repo->relativePath(path), buffer);
    }));
}

QString MainWindow::gitStatusPath() const
{
    // The current note's repository; with no note open, the vault's.
    if (const Editor* editor = currentEditor())
        return editor->path();
    if (!m_history->notePath().isEmpty())
        return m_history->notePath();
    return m_vault ? m_vault->rootPath() : QString();
}

void MainWindow::refreshGitStatus()
{
    const QString path = gitStatusPath();
    const int generation = ++m_gitStatusGeneration;
    if (path.isEmpty()) {
        m_gitButton->hide();
        return;
    }
    struct Result {
        bool inRepository = false;
        QString workdir;
        int changes = 0;
    };
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_gitStatusGeneration)
            return;
        const Result result = watcher->result();
        m_gitButton->setVisible(result.inRepository && result.changes > 0);
        m_gitButton->setText(tr("● %n file(s) changed", nullptr, result.changes));
        if (!m_push->isRunning() && !m_gitButton->toolTip().startsWith(tr("Last push failed")))
            m_gitButton->setToolTip(tr("Uncommitted changes in %1 — click to commit (Ctrl+Alt+C)").arg(result.workdir));
        emit gitStatusUpdated(result.inRepository ? result.changes : -1);
    });
    watcher->setFuture(QtConcurrent::run(m_gitPool, [path] {
        Result result;
        const auto repo = GitRepository::discover(path);
        if (!repo)
            return result;
        result.inRepository = true;
        result.workdir = repo->workdir();
        if (const auto changes = repo->status())
            result.changes = int(changes->size());
        return result;
    }));
}

void MainWindow::openCommitDialog()
{
    const QString path = gitStatusPath();
    if (path.isEmpty())
        return;
    if (!m_commitDialog) {
        m_commitDialog = new CommitDialog(this);
        connect(m_commitDialog, &CommitDialog::commitRequested, this, &MainWindow::commit);
        connect(m_commitDialog, &CommitDialog::saveAllRequested, this, [this](const QString& workdir) {
            for (int i = 0; i < m_tabs->count(); ++i) {
                if (Editor* editor = editorAt(i); editor && editor->isModified() && editor->path().startsWith(workdir + QLatin1Char('/')))
                    saveEditor(editor);
            }
            openCommitDialog(); // read the changes again, now with the saves
        });
    }
    // Notes open with unsaved changes: their paths, to warn that the commit leaves them out.
    QList<QPair<QString, bool>> open;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (const Editor* editor = editorAt(i))
            open.append({editor->path(), editor->isModified()});
    }
    struct Result {
        QString error;
        QString workdir;
        QString branch;
        QList<GitChange> changes;
        QStringList unsaved;
    };
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        watcher->deleteLater();
        const Result result = watcher->result();
        if (!result.error.isEmpty()) {
            statusBar()->showMessage(result.error, kStatusTimeoutMs * 2);
            return;
        }
        if (result.changes.isEmpty() && result.unsaved.isEmpty()) {
            statusBar()->showMessage(tr("Nothing to commit in %1").arg(result.workdir), kStatusTimeoutMs);
            m_commitDialog->hide();
            return;
        }
        m_commitDialog->setRepository(result.workdir, result.branch, result.changes, result.unsaved);
        m_commitDialog->setBusy(false);
        m_commitDialog->show();
        m_commitDialog->raise();
        m_commitDialog->activateWindow();
        emit commitDialogReady();
    });
    watcher->setFuture(QtConcurrent::run(m_gitPool, [path, open] {
        Result result;
        const auto repo = GitRepository::discover(path);
        if (!repo) {
            result.error = repo.error();
            return result;
        }
        result.workdir = repo->workdir();
        result.branch = repo->branch();
        const auto changes = repo->status();
        if (!changes) {
            result.error = changes.error();
            return result;
        }
        result.changes = *changes;
        for (const auto& [notePath, modified] : open) {
            if (modified && notePath.startsWith(result.workdir + QLatin1Char('/')))
                result.unsaved.append(repo->relativePath(notePath));
        }
        return result;
    }));
}

void MainWindow::commit(const QString& workdir, const QStringList& paths, const QString& message, bool push)
{
    m_commitDialog->setBusy(true, tr("Committing…"));
    using Result = std::expected<GitCommit, QString>;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, workdir, push] {
        watcher->deleteLater();
        const Result made = watcher->result();
        if (!made) {
            m_commitDialog->setBusy(false);
            m_commitDialog->showError(made.error());
            return;
        }
        m_commitDialog->messageEdit()->clear();
        m_commitDialog->hide();
        statusBar()->showMessage(tr("Committed %1: %2").arg(made->shortId, made->summary), kStatusTimeoutMs * 2);
        m_history->refresh();
        refreshGitStatus();
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (Editor* editor = editorAt(i); editor && editor->blameShown())
                requestBlame(editor); // their "Not committed" lines may be committed now
        }
        emit committed(made->id);
        if (push) {
            if (m_push->start(workdir, {QStringLiteral("push")}))
                statusBar()->showMessage(tr("Committed %1; pushing…").arg(made->shortId));
            else
                statusBar()->showMessage(tr("Committed %1; a push is already running").arg(made->shortId), kStatusTimeoutMs * 2);
        }
    });
    watcher->setFuture(QtConcurrent::run(m_gitPool, [workdir, paths, message]() -> Result {
        auto repo = GitRepository::discover(workdir);
        if (!repo)
            return std::unexpected(repo.error());
        return repo->commit(paths, message);
    }));
}

void MainWindow::refreshHeader(const QString& readmePath, const QString& projectLabel)
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    auto* box = new QMessageBox(QMessageBox::Question, tr("Refresh header"),
                                tr("Set Last reviewed of %1 to %2?").arg(projectLabel, today),
                                QMessageBox::Yes | QMessageBox::Cancel, this);
    box->setObjectName(QStringLiteral("refreshHeaderPrompt"));
    box->setInformativeText(tr("Only that line of %1 changes.").arg(displayPath(readmePath)));
    box->setDefaultButton(QMessageBox::Yes);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QMessageBox::finished, this, [this, box, readmePath] {
        if (box->clickedButton() != box->button(QMessageBox::Yes))
            return;
        const QDate date = QDate::currentDate();
        if (Editor* editor = editorFor(readmePath)) {
            // Open: change the buffer (one undo step). A clean buffer is then
            // saved, through the conflict check as any save (AV-002); one with
            // the author's unsaved edits is left for them to save.
            const bool hadEdits = editor->isModified();
            const auto updated = ProjectScaffold::withLastReviewed(editor->toPlainText(), date);
            if (!updated) {
                statusBar()->showMessage(tr("%1 does not open with a status header").arg(displayPath(readmePath)), kStatusTimeoutMs * 2);
                return;
            }
            QTextCursor cursor(editor->document()->findBlockByNumber(2)); // the Last reviewed line
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.insertText(updated->section(QLatin1Char('\n'), 2, 2).remove(QLatin1Char('\r')));
            cursor.endEditBlock();
            if (hadEdits) {
                statusBar()->showMessage(tr("Last reviewed set in the open %1, which has other unsaved changes: save it to keep it")
                                             .arg(displayPath(readmePath)),
                                         kStatusTimeoutMs * 3);
                emit headerRefreshed(readmePath);
                return;
            }
            if (!saveEditor(editor))
                return;
        } else {
            const Vault* root = rootFor(readmePath);
            QFile file(readmePath);
            if (!root || !file.open(QIODevice::ReadOnly)) {
                statusBar()->showMessage(tr("Cannot read %1").arg(displayPath(readmePath)), kStatusTimeoutMs * 2);
                return;
            }
            const auto updated = ProjectScaffold::withLastReviewed(QString::fromUtf8(file.readAll()), date);
            file.close();
            if (!updated) {
                statusBar()->showMessage(tr("%1 does not open with a status header").arg(displayPath(readmePath)), kStatusTimeoutMs * 2);
                return;
            }
            if (const auto written = root->writeNote(readmePath, updated->toUtf8()); !written) {
                statusBar()->showMessage(written.error(), kStatusTimeoutMs * 2);
                return;
            }
        }
        statusBar()->showMessage(tr("Last reviewed set to %1 in %2").arg(date.toString(Qt::ISODate), displayPath(readmePath)),
                                 kStatusTimeoutMs);
        emit headerRefreshed(readmePath);
    });
    box->open();
}

void MainWindow::promoteIdea(const IdeaRef& idea)
{
    if (!m_vault)
        return;
    auto* dialog = new PromoteDialog(idea, m_vault->rootPath(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        promote(dialog->idea(), dialog->codename(), dialog->targetFolder(),
                dialog->toLinkedRoot() ? dialog->rootName() : QString());
    });
    dialog->open();
}

bool MainWindow::promote(const IdeaRef& idea, const QString& codename, const QString& targetFolder, const QString& rootName)
{
    const auto fail = [this](const QString& message) {
        statusBar()->showMessage(tr("Not promoted: %1").arg(message), kStatusTimeoutMs * 3);
        return false;
    };
    if (!m_vault || targetFolder.isEmpty())
        return fail(tr("no folder to create"));
    if (QFileInfo::exists(targetFolder) && !QDir(targetFolder).isEmpty())
        return fail(tr("%1 already exists").arg(targetFolder));
    if (Editor* open = editorFor(idea.notePath); open && open->isModified())
        return fail(tr("the idea has unsaved changes; save or discard them first"));
    const bool linked = !rootName.isEmpty();
    if (linked) {
        // The root's name and place are checked against config.toml before anything is written.
        QByteArray config;
        if (QFile file(Roots::configPath(m_vault->rootPath())); file.open(QIODevice::ReadOnly))
            config = file.readAll();
        if (const auto check = Roots::withRoot(config, m_vault->rootPath(), rootName, targetFolder); !check)
            return fail(check.error());
        if (targetFolder.startsWith(m_vault->rootPath() + QLatin1Char('/')))
            return fail(tr("a linked root lies outside the vault"));
    } else if (!targetFolder.startsWith(m_vault->rootPath() + QLatin1Char('/'))) {
        return fail(tr("%1 is outside the vault").arg(targetFolder));
    }

    QFile ideaFile(idea.notePath);
    if (!ideaFile.open(QIODevice::ReadOnly))
        return fail(tr("cannot read %1").arg(displayPath(idea.notePath)));
    const QString ideaText = QString::fromUtf8(ideaFile.readAll());
    ideaFile.close();
    if (!QDir().mkpath(targetFolder))
        return fail(tr("cannot create %1").arg(targetFolder));
    const auto target = linked ? Vault::open(targetFolder) : std::expected<Vault, QString>(*m_vault);
    if (!target)
        return fail(target.error());
    const auto files = ProjectScaffold::promotion(ideaText, m_vault->relativePath(idea.notePath), codename, QDate::currentDate());
    for (const auto& [name, content] : files) {
        if (const auto written = target->writeNote(targetFolder + QLatin1Char('/') + name, content.toUtf8()); !written)
            return fail(written.error());
    }
    // Every file is written: only now does the idea note go (SPEC.md §Ideas).
    if (const int tab = tabIndexFor(idea.notePath); tab >= 0)
        closeTab(tab);
    if (!QFile::remove(idea.notePath))
        statusBar()->showMessage(tr("Promoted, but %1 could not be removed").arg(displayPath(idea.notePath)), kStatusTimeoutMs * 3);
    const QString readme = targetFolder + QStringLiteral("/README.md");
    if (linked) {
        if (const auto added = Roots::add(m_vault->rootPath(), rootName, targetFolder); !added)
            return fail(tr("the project was created at %1 but not linked: %2").arg(targetFolder, added.error()));
        openVault(*m_vault); // reopened, so the new root is watched and indexed
    }
    openNote(readme);
    statusBar()->showMessage(tr("Promoted %1 to %2").arg(codename, linked ? tr("the linked root %1").arg(rootName) : displayPath(targetFolder)),
                             kStatusTimeoutMs * 2);
    emit promoted(readme);
    return true;
}

QList<WriteToast*> MainWindow::toasts() const
{
    QList<WriteToast*> live;
    for (const QPointer<WriteToast>& toast : m_toasts) {
        if (toast)
            live.append(toast);
    }
    return live;
}

void MainWindow::showWriteToast(const QString& path, const std::optional<QByteArray>& before, const QByteArray& afterHash,
                                const QString& summary)
{
    auto* toast = new WriteToast(path, before, afterHash, summary, this);
    connect(toast, &WriteToast::undoRequested, this, &MainWindow::undoWrite);
    connect(toast, &QObject::destroyed, this, [this] { QMetaObject::invokeMethod(this, &MainWindow::layoutToasts, Qt::QueuedConnection); });
    m_toasts.append(toast);
    toast->show();
    layoutToasts();
    emit toastShown(toast);
}

void MainWindow::undoWrite(WriteToast* toast)
{
    const QString path = toast->path();
    QFile file(path);
    const bool exists = file.exists();
    QByteArray current;
    if (exists && file.open(QIODevice::ReadOnly))
        current = file.readAll();
    file.close();
    // Only what was written is put back: if anything changed it since, it stays.
    if (!exists || ContentHash::of(current) != toast->afterHash()) {
        toast->finish(tr("Not undone: %1 has changed since").arg(displayPath(path)));
        emit writeUndone(path, false);
        return;
    }
    bool undone = false;
    if (toast->before()) {
        const Vault* root = rootFor(path);
        undone = root && root->writeNote(path, *toast->before()).has_value();
    } else {
        undone = QFile::remove(path); // it was created by the write: undoing removes it
    }
    toast->finish(undone ? tr("Undone: %1 is as it was").arg(displayPath(path)) : tr("Could not undo %1").arg(displayPath(path)));
    emit writeUndone(path, undone);
}

void MainWindow::layoutToasts()
{
    // Stacked upwards from the bottom-right corner of the editor area.
    const QRect area = centralWidget() ? QRect(centralWidget()->mapTo(this, QPoint(0, 0)), centralWidget()->size()) : rect();
    int bottom = area.bottom() - 12;
    for (const QPointer<WriteToast>& toast : std::as_const(m_toasts)) {
        if (!toast || !toast->isVisible())
            continue;
        // Within the editor area, never over the docks beside it.
        const int width = std::max(200, std::min(toast->maximumWidth(), area.width() - 24));
        toast->setFixedWidth(width);
        const QSize size(width, toast->heightForWidth(width) > 0 ? toast->heightForWidth(width) : toast->sizeHint().height());
        toast->setGeometry(area.right() - 12 - size.width(), bottom - size.height(), size.width(), size.height());
        toast->raise();
        bottom -= size.height() + 8;
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    layoutToasts();
}

void MainWindow::linkHere(const QString& absolutePath)
{
    Editor* editor = currentEditor();
    if (!editor)
        return;
    const NoteEntry* target = nullptr;
    for (const NoteEntry& note : m_catalogue->notes()) {
        if (note.rootPath + QLatin1Char('/') + note.path == absolutePath)
            target = &note;
    }
    if (!target) {
        statusBar()->showMessage(tr("%1 is not indexed yet").arg(displayPath(absolutePath)), kStatusTimeoutMs);
        return;
    }
    const QString source = m_catalogue->locate(editor->path()).rootName;
    editor->insertPlainText(QStringLiteral("[[") + WikilinkCompletion::linkText(*target, m_catalogue->notes(), source) + QStringLiteral("]]"));
    editor->setFocus();
}

void MainWindow::changeEvent(QEvent* event)
{
    // Commits made elsewhere (a terminal, Claude Code) show on coming back.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_history) {
        m_history->refresh();
        refreshGitStatus();
        if (m_boardDock && m_boardDock->isVisible())
            m_board->refresh(); // commits, pulls and reviews made elsewhere
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmCloseAll()) {
        event->ignore();
        return;
    }
    m_journalPool->waitForDone(); // pending journal writes and removals land first
    saveLayout();
    QMainWindow::closeEvent(event);
}

} // namespace coeus
