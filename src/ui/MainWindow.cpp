#include "ui/MainWindow.hpp"

#include "ui/CommandPalette.hpp"
#include "ui/ExplorerPanel.hpp"
#include "index/IndexService.hpp"
#include "ui/SidebarPanel.hpp"
#include "vault/GitIgnoreOffer.hpp"
#include "vault/Roots.hpp"
#include "vault/TempSweeper.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
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

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* page = m_tabs->widget(index);
        m_tabs->removeTab(index);
        page->deleteLater();
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatus);

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
    m_sidebar->addSection(tr("Backlinks"), placeholder(tr("Backlinks arrive in Phase 2.")));
    m_sidebar->addSection(tr("Outline"), placeholder(tr("Outline arrives in Phase 2.")));
    m_sidebar->addSection(tr("Properties"), placeholder(tr("Properties arrive in Phase 2.")));
    m_sidebarDock = new QDockWidget(tr("Sidebar"), this);
    m_sidebarDock->setObjectName(QStringLiteral("sidebarDock"));
    m_sidebarDock->setWidget(m_sidebar);
    addDockWidget(Qt::RightDockWidgetArea, m_sidebarDock);

    resizeDocks({m_explorerDock, m_sidebarDock}, {260, 300}, Qt::Horizontal);

    connect(m_explorer, &ExplorerPanel::fileActivated, this, &MainWindow::openNote);
}

void MainWindow::createActions()
{
    auto* openVault = new QAction(QIcon::fromTheme(QStringLiteral("document-open-folder")),
                                  tr("&Open Vault…"), this);
    openVault->setShortcut(QKeySequence::Open);
    connect(openVault, &QAction::triggered, this, &MainWindow::chooseVault);

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
    fileMenu->addAction(closeTab);
    fileMenu->addSeparator();
    fileMenu->addAction(m_verifyIndex);
    fileMenu->addAction(m_rebuildIndex);
    fileMenu->addAction(m_gitignoreAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quit);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(palette);
    viewMenu->addSeparator();
    viewMenu->addAction(toggleExplorer);
    viewMenu->addAction(toggleSidebar);

    auto* strip = new QToolBar(tr("Tools"), this);
    strip->setObjectName(QStringLiteral("toolStrip"));
    strip->setMovable(false);
    strip->setIconSize(QSize(18, 18));
    strip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    strip->addAction(toggleExplorer);
    strip->addAction(palette);
    strip->addAction(toggleSidebar);
    addToolBar(Qt::LeftToolBarArea, strip);

    for (QAction* action : {openVault, closeTab, quit, palette, toggleExplorer, toggleSidebar, m_verifyIndex, m_rebuildIndex, m_gitignoreAction}) {
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
    while (m_tabs->count() > 0) {
        QWidget* page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        page->deleteLater();
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
    connect(m_indexThread, &QThread::finished, m_index, &QObject::deleteLater);

    connect(m_index, &IndexService::busy, this, [this](const QString& what) { setIndexStatus(what); });
    connect(m_index, &IndexService::opened, this, [this](int notes, const QStringList& problems) {
        setIndexStatus(tr("index ok · %n note(s)", nullptr, notes), problems.join(QLatin1Char('\n')));
        if (!problems.isEmpty())
            statusBar()->showMessage(problems.front(), kStatusTimeoutMs * 2);
        m_verifyIndex->setEnabled(true);
        m_rebuildIndex->setEnabled(true);
        emit indexOpened(notes);
    });
    connect(m_index, &IndexService::indexChanged, this, [this](int notes) {
        setIndexStatus(tr("index ok · %n note(s)", nullptr, notes), m_indexLabel->toolTip());
        emit indexUpdated(notes);
    });
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
    if (!m_vault)
        return;

    if (const int existing = tabIndexFor(absolutePath); existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }

    const Vault* root = rootFor(absolutePath);
    if (!root) {
        statusBar()->showMessage(tr("Outside the vault and its linked roots: %1").arg(absolutePath), kStatusTimeoutMs);
        return;
    }

    if (!Vault::isNote(absolutePath)) {
        statusBar()->showMessage(tr("Attachments cannot be previewed yet: %1").arg(displayPath(absolutePath)),
                                 kStatusTimeoutMs);
        return;
    }

    const auto text = root->readNote(absolutePath);
    if (!text) {
        statusBar()->showMessage(text.error(), kStatusTimeoutMs);
        return;
    }

    auto* editor = new QPlainTextEdit;
    editor->setReadOnly(true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlainText(*text);
    editor->setProperty(kPathProperty, absolutePath);

    const int index = m_tabs->addTab(editor, QFileInfo(absolutePath).completeBaseName());
    m_tabs->setTabToolTip(index, displayPath(absolutePath));
    m_tabs->setCurrentIndex(index);
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
        emit m_tabs->tabCloseRequested(index);
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
    saveLayout();
    QMainWindow::closeEvent(event);
}

} // namespace coeus
