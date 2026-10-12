#include "ui/MainWindow.hpp"

#include "ui/CommandPalette.hpp"
#include "ui/ExplorerPanel.hpp"
#include "ui/SidebarPanel.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

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

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openVault);
    fileMenu->addAction(closeTab);
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

    for (QAction* action : {openVault, closeTab, quit, palette, toggleExplorer, toggleSidebar}) {
        addAction(action); // keeps shortcuts live when the menu bar is hidden
        registerAction(action);
    }
}

void MainWindow::createStatusBar()
{
    m_noteLabel = new QLabel;
    m_vaultLabel = new QLabel;
    statusBar()->addWidget(m_noteLabel, 1);
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
    m_explorer->setRootPath(vault.rootPath());
    setWindowTitle(QStringLiteral("%1 — Coeus").arg(vault.name()));
    updateStatus();
}

void MainWindow::openNote(const QString& absolutePath)
{
    if (!m_vault)
        return;

    if (const int existing = tabIndexFor(absolutePath); existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }

    if (!Vault::isNote(absolutePath)) {
        statusBar()->showMessage(tr("Attachments cannot be previewed yet: %1")
                                     .arg(m_vault->relativePath(absolutePath)),
                                 kStatusTimeoutMs);
        return;
    }

    const auto text = m_vault->readNote(absolutePath);
    if (!text) {
        statusBar()->showMessage(text.error(), kStatusTimeoutMs);
        return;
    }

    auto* editor = new QPlainTextEdit;
    editor->setReadOnly(true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlainText(*text);
    editor->setProperty(kPathProperty, absolutePath);

    const QString relative = m_vault->relativePath(absolutePath);
    const int index = m_tabs->addTab(editor, QFileInfo(absolutePath).completeBaseName());
    m_tabs->setTabToolTip(index, relative);
    m_tabs->setCurrentIndex(index);
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
    m_noteLabel->setText(page ? m_vault->relativePath(page->property(kPathProperty).toString())
                              : QString());
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
