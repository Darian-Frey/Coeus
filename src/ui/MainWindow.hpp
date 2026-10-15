#pragma once

#include "ui/NoteView.hpp"
#include "vault/Bookmarks.hpp"
#include "vault/RecentNotes.hpp"
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
class QToolButton;
class QMessageBox;
class QTabWidget;
class QThread;
class QThreadPool;
class QTimer;
class QUrl;

namespace coeus {

class BacklinksPanel;
class OutlinePanel;
class TagPanel;
class HistoryPanel;
class EmbeddingService;
class ProblemsPanel;
class ProjectBoard;
class McpLink;
class PromoteDialog;
class WriteToast;
struct IdeaRef;
class CommitDialog;
class DiffView;
class GitCommand;
class VersionView;
struct GitCommit;
class RegisterPanel;
class RelatedPanel;
struct ProjectRef;
class SearchPanel;
class BookmarksPanel;
class RenderService;
class CommandPalette;
class Editor;
class NoteCatalogue;
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
    // Opens today's daily note, creating it first if needed (F-009).
    void openTodaysNote();
    // Offers the templates folder's notes; the chosen one goes in at the cursor (F-010).
    void chooseTemplate();
    void insertTemplate(const QString& templatePath);
    // File › New Note… (F-036): the dialog, then createNote().
    void newNote();
    // View › Keyboard Shortcuts… (F-027).
    void editShortcuts();
    // View › Default Mode (F-003): the mode notes open in, kept per user; Live at first.
    NoteView::Mode defaultMode() const;
    void setDefaultMode(NoteView::Mode mode);
    // Bookmarks and recent notes (F-014).
    void toggleBookmark(const QString& absolutePath);
    bool isBookmarked(const QString& absolutePath) const;
    void openBookmark(int index);
    void chooseRecent();
    const QList<Bookmark>& bookmarks() const { return m_bookmarkList; }
    const QList<RecentNotes::Note>& recentNotes() const { return m_recentList; }
    BookmarksPanel* bookmarksPanel() const { return m_bookmarks; }
    RenderService* renderService() const { return m_render; }
    CommandPalette* recentPicker() const { return m_recentPicker; }
    // Creates the note at `absolutePath` from `templatePath` (empty for
    // blank), allocating any {{next:KIND}} first, and opens it. Never
    // overwrites; emits noteCreated() once it is written.
    void createNote(const QString& absolutePath, const QString& templatePath);
    // Offers the register kinds; the next free ID of the chosen kind is
    // appended to the current project's register file as a new entry (F-015).
    void chooseNextId();
    void insertNextId(const QString& kind);
    CommandPalette* nextIdPicker() const { return m_nextIdPicker; }
    RegisterPanel* registers() const { return m_registers; }
    ProblemsPanel* problems() const { return m_problems; }
    ProjectBoard* board() const { return m_board; }
    // F-032 board commands, each an explicit user command (CLAUDE.md §Invariants).
    // Asks, then sets the README's Last reviewed to today.
    void refreshHeader(const QString& readmePath, const QString& projectLabel);
    // Asks for the codename and place, then promotes the idea.
    void promoteIdea(const IdeaRef& idea);
    // Writes the new project and removes the idea; true on success.
    bool promote(const IdeaRef& idea, const QString& codename, const QString& targetFolder, const QString& rootName);
    // F-033: the link `coeus --mcp` uses to ask about open notes and report writes.
    McpLink* mcpLink() const { return m_mcpLink; }
    QList<WriteToast*> toasts() const;
    QDockWidget* boardDock() const { return m_boardDock; }
    HistoryPanel* history() const { return m_history; }
    RelatedPanel* related() const { return m_related; }
    // F-019: a link to `absolutePath` at the cursor of the current note, resolving to it (AV-006).
    void linkHere(const QString& absolutePath);
    // Opens the note as it was in `commit`, read-only, in a tab (F-020).
    void openVersion(const QString& notePath, const GitCommit& commit, const QString& workdir);
    // F-021: one side of a comparison. Nothing is an empty text; the working
    // copy is the open editor's buffer, or the file if the note is not open.
    struct DiffSide {
        enum class Kind { Nothing, Revision, WorkingCopy };
        Kind kind = Kind::WorkingCopy;
        QString revision; // a commit id or "HEAD"
        QString path;     // workdir-relative path in that revision; "" for the note's current one
        QString label;    // "HEAD", "a41c9e2", "working copy"
    };
    // Compares two sides of the note at `notePath` in a DiffView tab.
    void openDiff(const QString& notePath, const DiffSide& older, const DiffSide& newer);
    void diffWithHead(const QString& notePath);
    // Opens or closes the blame gutter of the current editor.
    void setBlameShown(bool shown);
    // F-022: the commit dialog for the current note's repository (or the
    // vault's), and the status-bar count of changed files.
    void openCommitDialog();
    void refreshGitStatus();
    CommitDialog* commitDialog() const { return m_commitDialog; }
    QToolButton* gitButton() const { return m_gitButton; }
    QDockWidget* problemsDock() const { return m_problemsDock; }
    QToolButton* problemsButton() const { return m_problemsButton; }
    CommandPalette* templatePicker() const { return m_templatePicker; }
    // Opens a note with the cursor on `line` (1-based), e.g. from a backlink.
    void openNoteAt(const QString& absolutePath, int line);
    // Opens the note at `anchor`: a heading's text, or ^block (F-024); at the
    // top, saying so, if the note has no such anchor.
    void openAnchor(const QString& absolutePath, const QString& anchor);
    // Copy Block Link (F-024): [[Note#^id]] for the paragraph or list item at
    // the cursor, which gets an ID first if it has none.
    void copyBlockLink();
    BacklinksPanel* backlinks() const { return m_backlinks; }
    OutlinePanel* outline() const { return m_outline; }
    TagPanel* tags() const { return m_tags; }
    SearchPanel* search() const { return m_search; }
    // Opens a note and selects the first of `words` it contains (search hits).
    void openNoteAtWords(const QString& absolutePath, const QStringList& words);
    // Filters the explorer to notes tagged `tag` or beneath it (F-007).
    void showTag(const QString& tag);
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
    NoteCatalogue* catalogue() const { return m_catalogue; }

signals:
    void versionOpened(coeus::VersionView* view);
    void diffOpened(coeus::DiffView* view);
    void blameApplied(coeus::Editor* editor);
    void gitStatusUpdated(int changes);
    void commitDialogReady();
    void committed(const QString& commitId);
    void pushFinished(bool ok, const QString& message);
    void headerRefreshed(const QString& readmePath);
    void promoted(const QString& readmePath);
    void toastShown(coeus::WriteToast* toast);
    void writeUndone(const QString& path, bool undone);
    // An ID was reserved and its entry appended to the register's buffer (F-015).
    void nextIdInserted(const QString& ident, const QString& registerFile);
    // A template was inserted into the current buffer (F-010).
    void templateInserted(const QString& templatePath);
    void noteCreated(const QString& absolutePath);
    // The stale temporary-file sweep started by openVault() has finished (IMP-002).
    void tempSweepFinished(int removed);
    // The index worker opened the vault's index and finished its first scan.
    void indexOpened(int noteCount);
    // F-018: how many passages have vectors, as the embedding worker reports it.
    void embeddingProgress(int embedded, int total);
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
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showWriteToast(const QString& path, const std::optional<QByteArray>& before, const QByteArray& afterHash,
                        const QString& summary);
    void undoWrite(WriteToast* toast);
    void layoutToasts();
    void requestBlame(Editor* editor);
    QString gitStatusPath() const;
    void commit(const QString& workdir, const QStringList& paths, const QString& message, bool push);

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
    // {{next:KIND}} IDs for `kinds` in `project`, allocated by NextId on a
    // worker thread, then `done(ids, error)` on the UI thread.
    void allocateTemplateIds(const ProjectRef& project, const QStringList& kinds,
                             std::function<void(const QHash<QString, QString>&, const QString&)> done);
    void applyTemplate(Editor* editor, const QString& templatePath, const QString& source,
                       const QHash<QString, QString>& nextIds);
    // Unsaved text of open buffers of `kind`'s register file in `project`.
    QString unsavedRegisterText(const ProjectRef& project, const QString& kind) const;
    void showCommandPalette();
    void updateStatus();
    void updateProblemsButton();
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
    BacklinksPanel* m_backlinks = nullptr;
    OutlinePanel* m_outline = nullptr;
    TagPanel* m_tags = nullptr;
    QDockWidget* m_tagsDock = nullptr;
    SearchPanel* m_search = nullptr;
    QDockWidget* m_searchDock = nullptr;
    QString m_activeTag; // the tag the explorer is filtered by, if any
    CommandPalette* m_palette = nullptr;
    QLabel* m_noteLabel = nullptr;
    QLabel* m_vaultLabel = nullptr;
    QLabel* m_indexLabel = nullptr;
    QToolButton* m_problemsButton = nullptr;
    QToolButton* m_gitButton = nullptr;
    CommitDialog* m_commitDialog = nullptr;
    GitCommand* m_push = nullptr;
    QTimer* m_gitStatusTimer = nullptr;
    int m_gitStatusGeneration = 0;
    QThread* m_indexThread = nullptr;
    IndexService* m_index = nullptr;
    QAction* m_verifyIndex = nullptr;
    QAction* m_rebuildIndex = nullptr;
    QAction* m_compactSidecar = nullptr; // F-035
    QList<QAction*> m_themeActions;      // System, Light, Dark, Cassette (F-027)
    QAction* m_copyBlockLink = nullptr; // F-024; also in each editor's context menu
    RenderService* m_render = nullptr;  // D-022
    // F-014.
    BookmarksPanel* m_bookmarks = nullptr;
    QDockWidget* m_bookmarksDock = nullptr;
    QList<Bookmark> m_bookmarkList;
    QList<RecentNotes::Note> m_recentList;
    QTimer* m_recentTimer = nullptr;
    CommandPalette* m_recentPicker = nullptr;
    QObject* m_recentActions = nullptr; // the picker's current list
    QMenu* m_recentMenu = nullptr;
    static constexpr int kRecentSaveDelayMs = 2000;
    std::optional<std::pair<QString, QString>> rootRelative(const QString& absolutePath) const;
    QString absoluteOf(const QString& rootName, const QString& path) const;
    void setBookmarks(const QList<Bookmark>& bookmarks);
    void presentBookmarks();
    void noteRecent(const QString& absolutePath);
    void saveRecent();
    QList<QAction*> recentActions(QObject* parent);
    QAction* m_gitignoreAction = nullptr;
    QAction* m_save = nullptr;
    QAction* m_toggleReading = nullptr;
    CommandPalette* m_templatePicker = nullptr;
    CommandPalette* m_nextIdPicker = nullptr;
    QObject* m_nextIdActions = nullptr;
    RegisterPanel* m_registers = nullptr;
    QDockWidget* m_registersDock = nullptr;
    ProblemsPanel* m_problems = nullptr;
    HistoryPanel* m_history = nullptr;
    RelatedPanel* m_related = nullptr;
    QTimer* m_relatedTimer = nullptr;
    QThreadPool* m_gitPool = nullptr;
    QDockWidget* m_problemsDock = nullptr;
    ProjectBoard* m_board = nullptr;
    QDockWidget* m_boardDock = nullptr;
    QTimer* m_boardTimer = nullptr;
    McpLink* m_mcpLink = nullptr;
    QThread* m_embedThread = nullptr;
    EmbeddingService* m_embedding = nullptr;
    QLabel* m_semanticLabel = nullptr;
    QList<QPointer<WriteToast>> m_toasts;
    QObject* m_templateActions = nullptr; // parent of the picker's per-template actions
    NoteCatalogue* m_catalogue = nullptr; // notes and aliases for [[ completion
    QTimer* m_journalTimer = nullptr;
    QThreadPool* m_journalPool = nullptr; // one thread: journal writes stay in order
    QHash<Editor*, int> m_journalled;     // document revision last written to the journal
    // The conflict prompt showing for an editor, if any. QPointer clears itself
    // when the prompt closes, so no destroyed() handler touches members that
    // may already be gone during ~MainWindow.
    QHash<Editor*, QPointer<QMessageBox>> m_conflictPrompts;
};

} // namespace coeus
