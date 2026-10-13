#pragma once

#include "vault/Vault.hpp"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>

#include <optional>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QMessageBox;
class QTabWidget;
class QThread;
class QThreadPool;
class QTimer;
class QUrl;

namespace coeus {

class CommandPalette;
class Editor;
class NoteView;
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
    // Opens a note in an editor tab, or focuses the tab if it is already open.
    void openNote(const QString& absolutePath);
    // Saves the current tab's note; false if it was not saved.
    bool saveCurrent();
    // Closes the current tab, asking first if it has unsaved changes.
    void closeCurrentTab();

    NoteView* viewAt(int index) const;
    NoteView* currentView() const;
    Editor* editorAt(int index) const;
    Editor* currentEditor() const;
    Editor* editorFor(const QString& absolutePath) const;

    // Autosave journal interval (SPEC.md §Constants autosave_interval_s).
    static constexpr int kJournalIntervalMs = 5000;
    // Writes every changed buffer to the journal now; for tests and shutdown.
    void journalNow();
    void waitForJournal();

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
    // The first-open .gitignore offer is showing (SPEC.md §Vault layout).
    void gitignoreOffered(const QStringList& patterns);
    // Patterns were written to the vault's .gitignore.
    void gitignoreUpdated(const QStringList& patterns);
    // An editor's buffer and the disk disagree; the prompt is showing (AV-002).
    void conflictPrompted(const QString& absolutePath);
    // Unsaved buffers from a previous session are offered for recovery.
    void recoveryOffered(const QStringList& absolutePaths);

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
    void checkGitignore(const QString& vaultPath, bool onOpen);
    void offerGitignore(const QString& vaultPath, const QStringList& patterns);
    void addToGitignore(const QString& vaultPath, const QStringList& patterns);
    void stopIndex();
    void setIndexStatus(const QString& text, const QString& toolTip = QString());
    bool closeTab(int index);           // false if the author cancelled
    bool confirmCloseAll();             // before quitting or switching vault
    bool saveEditor(Editor* editor);
    void promptConflict(Editor* editor, bool retrySave);
    void handleDiskChange(Editor* editor);
    void onFilesChanged(const QStringList& absolutePaths);
    void onRootRescanned(const QString& rootPath);
    void updateTabTitle(Editor* editor);
    NoteView* viewOf(const Editor* editor) const;
    void toggleReadingMode();
    void followLink(NoteView* view, const QUrl& url);
    Editor* openEditor(const QString& absolutePath, bool allowMissing);
    void checkRecovery(const QString& vaultPath);
    void forgetJournal(const QString& notePath);
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
    QAction* m_gitignoreAction = nullptr;
    QAction* m_save = nullptr;
    QAction* m_toggleReading = nullptr;
    QTimer* m_journalTimer = nullptr;
    QThreadPool* m_journalPool = nullptr; // one thread: journal writes stay in order
    QHash<Editor*, int> m_journalled;     // document revision last written to the journal
    // The conflict prompt showing for an editor, if any. QPointer clears itself
    // when the prompt closes, so no destroyed() handler touches members that
    // may already be gone during ~MainWindow.
    QHash<Editor*, QPointer<QMessageBox>> m_conflictPrompts;
};

} // namespace coeus
