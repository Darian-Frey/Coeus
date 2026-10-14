#pragma once

#include "git/GitRepository.hpp"

#include <QWidget>

#include <optional>

class QLabel;
class QMenu;
class QPushButton;
class QThreadPool;
class QToolButton;
class QTreeWidget;

namespace coeus {

// Sidebar section for F-020: the current note's commits, newest first, in
// the Git repository containing it, with the branch. libgit2 runs on the Git
// worker (`pool`, one thread), never the UI thread (AV-005); a newer request
// supersedes an older one still running. Activating a commit asks for that
// version of the note.
class HistoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPanel(QThreadPool* pool, QWidget* parent = nullptr);

    // The note whose history to show; empty for none. The note already shown
    // is not read again (refresh() does that).
    void showNote(const QString& absolutePath);
    // Reads the history again (after a commit, or on returning to the window).
    void refresh();

    QString notePath() const { return m_notePath; }
    QTreeWidget* tree() const { return m_tree; }
    QString branch() const;
    QString status() const;
    const QList<GitCommit>& commits() const { return m_commits; }
    QPushButton* diffButton() const { return m_diff; }
    QPushButton* blameButton() const { return m_blame; }
    // Reflects the current editor's blame gutter, without emitting.
    void setBlameChecked(bool checked);
    // The commit menu for the selected rows, as a right-click shows it (for tests).
    QMenu* commitMenu();

signals:
    // The note as it was in `commit`; `pathInCommit` is its workdir-relative
    // path there (it may have been renamed since), `workdir` the repository's.
    void versionRequested(const QString& absolutePath, const GitCommit& commit, const QString& workdir);
    // F-021: the working copy against HEAD; two versions, where a missing
    // `older` is nothing (the note's first commit) and a missing `newer` is the
    // working copy; the blame gutter on or off.
    void diffWithHeadRequested(const QString& absolutePath);
    void compareRequested(const QString& absolutePath, const std::optional<GitCommit>& older,
                          const std::optional<GitCommit>& newer);
    void blameToggled(bool on);
    // Results for the current note are on screen (for tests).
    void updated(int commits);

private:
    struct Result {
        QString error; // "Not in a Git repository", …
        QString workdir;
        QString branch;
        QList<GitCommit> commits;
    };
    void present(const Result& result);

    QThreadPool* m_pool;
    QString m_notePath;
    QString m_workdir;
    int m_generation = 0;
    QLabel* m_branch = nullptr;
    QLabel* m_status = nullptr;
    QToolButton* m_refresh = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_diff = nullptr;
    QPushButton* m_blame = nullptr;
    QMenu* m_menu = nullptr;
    QList<GitCommit> m_commits;
};

} // namespace coeus
