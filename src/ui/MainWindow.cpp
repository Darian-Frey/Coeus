#include "ui/MainWindow.hpp"

#include "ui/BacklinksPanel.hpp"
#include "ui/CommandPalette.hpp"
#include "ui/Editor.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/NoteView.hpp"
#include "ui/OutlinePanel.hpp"
#include "index/IndexQueries.hpp"
#include "index/IndexService.hpp"
#include "ui/SidebarPanel.hpp"
#include "ui/TagPanel.hpp"
#include "vault/GitIgnoreOffer.hpp"
#include "vault/Journal.hpp"
#include "vault/Roots.hpp"
#include "vault/TempSweeper.hpp"

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
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTextBlock>
#include <QToolBar>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

// Bump when docks or toolbars change identity so stale layouts are ignored.
constexpr int kLayoutVersion = 1;
constexpr int kStatusTimeoutMs = 5000;

const char* const kPathProperty = "coeusPath";

QLabel* placeholder(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_palette(new CommandPalette(this))
{
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
    m_journalTimer = new QTimer(this);
    m_journalTimer->setInterval(kJournalIntervalMs);
    connect(m_journalTimer, &QTimer::timeout, this, &MainWindow::journalNow);
    m_journalTimer->start();
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatus);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        Editor* editor = currentEditor();
        m_backlinks->showNote(editor ? editor->path() : QString());
        m_outline->setView(currentView());
    });

    createDocks();
    createActions();
    createStatusBar();
    restoreLayout();
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
    m_outline = new OutlinePanel;
    m_sidebar->addSection(tr("Outline"), m_outline);
    m_sidebar->addSection(tr("Properties"), placeholder(tr("Properties arrive in Phase 2.")));
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
    m_explorerDock->raise();
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

    m_toggleReading = new QAction(tr("Toggle &Reading Mode"), this);
    m_toggleReading->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_toggleReading, &QAction::triggered, this, &MainWindow::toggleReadingMode);

    auto* closeTab = new QAction(tr("&Close Tab"), this);
    closeTab->setShortcut(QKeySequence::Close);
    connect(closeTab, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    auto* quit = new QAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("&Quit"), this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    auto* palette = new QAction(QIcon::fromTheme(QStringLiteral("system-search")),
                                tr("Command &Palette…"), this);
    palette->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(palette, &QAction::triggered, this, &MainWindow::showCommandPalette);

    QAction* toggleExplorer = m_explorerDock->toggleViewAction();
    toggleExplorer->setText(tr("Toggle &Explorer"));
    toggleExplorer->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
    toggleExplorer->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

    QAction* toggleSidebar = m_sidebarDock->toggleViewAction();
    toggleSidebar->setText(tr("Toggle &Sidebar"));
    toggleSidebar->setIcon(QIcon::fromTheme(QStringLiteral("view-right-pane"),
                                            QIcon::fromTheme(QStringLiteral("view-list-details"))));
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

    m_gitignoreAction = new QAction(tr("Add Coeus Files to .&gitignore"), this);
    m_gitignoreAction->setEnabled(false);
    connect(m_gitignoreAction, &QAction::triggered, this, [this] {
        if (m_vault)
            checkGitignore(m_vault->rootPath(), false);
    });

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openVault);
    fileMenu->addAction(m_save);
    fileMenu->addAction(closeTab);
    fileMenu->addSeparator();
    fileMenu->addAction(m_verifyIndex);
    fileMenu->addAction(m_rebuildIndex);
    fileMenu->addAction(m_gitignoreAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quit);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(palette);
    viewMenu->addAction(m_toggleReading);
    viewMenu->addSeparator();
    viewMenu->addAction(toggleExplorer);
    viewMenu->addAction(toggleSidebar);

    auto* strip = new QToolBar(tr("Tools"), this);
    strip->setObjectName(QStringLiteral("toolStrip"));
    strip->setMovable(false);
    strip->setIconSize(QSize(18, 18));
    strip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    strip->addAction(toggleExplorer);
    QAction* toggleTags = m_tagsDock->toggleViewAction();
    toggleTags->setText(tr("Toggle &Tags"));
    toggleTags->setIcon(QIcon::fromTheme(QStringLiteral("tag"), QIcon::fromTheme(QStringLiteral("bookmarks"))));
    toggleTags->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    strip->addAction(toggleTags);
    viewMenu->addAction(toggleTags);
    addAction(toggleTags);
    registerAction(toggleTags);
    strip->addAction(palette);
    strip->addAction(toggleSidebar);
    addToolBar(Qt::LeftToolBarArea, strip);

    for (QAction* action : {openVault, m_save, m_toggleReading, closeTab, quit, palette, toggleExplorer, toggleSidebar, m_verifyIndex, m_rebuildIndex, m_gitignoreAction}) {
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
    statusBar()->addPermanentWidget(m_indexLabel);
    statusBar()->addPermanentWidget(m_vaultLabel);
    updateStatus();
}

void MainWindow::registerAction(QAction* action)
{
    if (!action || m_actions.contains(action))
        return;
    m_actions.append(action);
    m_palette->setActions(m_actions);
}

void MainWindow::openVault(const Vault& vault)
{
    if (!confirmCloseAll())
        return;
    while (m_tabs->count() > 0) {
        NoteView* view = viewAt(0);
        m_journalled.remove(view->editor());
        m_conflictPrompts.remove(view->editor());
        m_tabs->removeTab(0);
        view->deleteLater();
    }
    m_vault = vault;
    m_linkedRoots.clear();

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
    m_backlinks->setIndexPath(IndexService::indexPath(vaultPath));
    m_tags->setIndexPath(IndexService::indexPath(vaultPath));
    connect(m_indexThread, &QThread::finished, m_index, &QObject::deleteLater);

    connect(m_index, &IndexService::busy, this, [this](const QString& what) { setIndexStatus(what); });
    connect(m_index, &IndexService::opened, this, [this](int notes, const QStringList& problems) {
        setIndexStatus(tr("index ok · %n note(s)", nullptr, notes), problems.join(QLatin1Char('\n')));
        if (!problems.isEmpty())
            statusBar()->showMessage(problems.front(), kStatusTimeoutMs * 2);
        m_verifyIndex->setEnabled(true);
        m_rebuildIndex->setEnabled(true);
        m_catalogue->reload();
        m_backlinks->refresh();
        m_tags->reload();
        emit indexOpened(notes);
    });
    connect(m_index, &IndexService::indexChanged, this, [this](int notes) {
        setIndexStatus(tr("index ok · %n note(s)", nullptr, notes), m_indexLabel->toolTip());
        m_catalogue->reload();
        m_backlinks->refresh();
        m_tags->reload();
        emit indexUpdated(notes);
    });
    connect(m_index, &IndexService::filesChanged, this, &MainWindow::onFilesChanged);
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

    m_indexThread->start();
    setIndexStatus(tr("Indexing…"));
    QMetaObject::invokeMethod(m_index, [service = m_index, vaultPath] { service->open(vaultPath); },
                              Qt::QueuedConnection);
}

void MainWindow::stopIndex()
{
    if (!m_indexThread)
        return;
    m_indexThread->quit();
    m_indexThread->wait();
    delete m_indexThread;
    m_indexThread = nullptr;
    m_index = nullptr; // deleted on its own thread as it finished
    m_verifyIndex->setEnabled(false);
    m_rebuildIndex->setEnabled(false);
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
    if (!editor)
        return true;
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
