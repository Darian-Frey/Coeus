#pragma once

#include "vault/Vault.hpp"

#include <QList>
#include <QMainWindow>

#include <optional>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QTabWidget;
class QThread;

namespace coeus {

class CommandPalette;
class ExplorerPanel;
class IndexService;
class SidebarPanel;

// The application shell (F-001). Every later feature is a panel or a
// command registered here.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

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

signals:
    // The stale temporary-file sweep started by openVault() has finished (IMP-002).
    void tempSweepFinished(int removed);
    // The index worker opened the vault's index and finished its first scan.
    void indexOpened(int noteCount);
    // The index worker reported a change in the number of indexed notes.
    void indexUpdated(int noteCount);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createDocks();
    void createActions();
    void createStatusBar();
    void restoreLayout();
    void saveLayout() const;

    void chooseVault();
    void sweepTempFiles(const QString& root);
    void startIndex(const QString& vaultPath);
    void stopIndex();
    void setIndexStatus(const QString& text, const QString& toolTip = QString());
    void closeCurrentTab();
    void showCommandPalette();
    void updateStatus();
    int tabIndexFor(const QString& absolutePath) const;
    // The open root containing `absolutePath` (the vault or a linked root), or null.
    const Vault* rootFor(const QString& absolutePath) const;
    // "<path>" inside the vault, "<root name>/<path>" inside a linked root (SPEC.md §Linked roots).
    QString displayPath(const QString& absolutePath) const;

    std::optional<Vault> m_vault;
    struct LinkedRoot {
        QString name;
        Vault vault;
    };
    std::vector<LinkedRoot> m_linkedRoots; // present linked roots from config.toml (F-034)
    QList<QAction*> m_actions;

    QTabWidget* m_tabs = nullptr;
    QDockWidget* m_explorerDock = nullptr;
    QDockWidget* m_sidebarDock = nullptr;
    ExplorerPanel* m_explorer = nullptr;
    SidebarPanel* m_sidebar = nullptr;
    CommandPalette* m_palette = nullptr;
    QLabel* m_noteLabel = nullptr;
    QLabel* m_vaultLabel = nullptr;
    QLabel* m_indexLabel = nullptr;
    QThread* m_indexThread = nullptr;
    IndexService* m_index = nullptr;
    QAction* m_verifyIndex = nullptr;
    QAction* m_rebuildIndex = nullptr;
};

} // namespace coeus
