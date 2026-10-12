#pragma once

#include "vault/Vault.hpp"

#include <QList>
#include <QMainWindow>

#include <optional>

class QAction;
class QDockWidget;
class QLabel;
class QTabWidget;

namespace coeus {

class CommandPalette;
class ExplorerPanel;
class SidebarPanel;

// The application shell (F-001). Every later feature is a panel or a
// command registered here.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openVault(const Vault& vault);
    // Opens a note read-only in a tab, or focuses the tab if it is already open.
    void openNote(const QString& absolutePath);

    // Adds an action to the command palette. Ownership stays with the caller.
    void registerAction(QAction* action);
    const QList<QAction*>& registeredActions() const { return m_actions; }

    const std::optional<Vault>& vault() const { return m_vault; }
    ExplorerPanel* explorer() const { return m_explorer; }
    QTabWidget* tabs() const { return m_tabs; }
    CommandPalette* commandPalette() const { return m_palette; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createDocks();
    void createActions();
    void createStatusBar();
    void restoreLayout();
    void saveLayout() const;

    void chooseVault();
    void closeCurrentTab();
    void showCommandPalette();
    void updateStatus();
    int tabIndexFor(const QString& absolutePath) const;

    std::optional<Vault> m_vault;
    QList<QAction*> m_actions;

    QTabWidget* m_tabs = nullptr;
    QDockWidget* m_explorerDock = nullptr;
    QDockWidget* m_sidebarDock = nullptr;
    ExplorerPanel* m_explorer = nullptr;
    SidebarPanel* m_sidebar = nullptr;
    CommandPalette* m_palette = nullptr;
    QLabel* m_noteLabel = nullptr;
    QLabel* m_vaultLabel = nullptr;
};

} // namespace coeus
