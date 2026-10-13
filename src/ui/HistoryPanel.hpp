#pragma once

#include "git/GitRepository.hpp"

#include <QWidget>

class QLabel;
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

    // The note whose history to show; empty for none.
    void showNote(const QString& absolutePath);
    // Reads the history again (after a commit, or on returning to the window).
    void refresh();

    QString notePath() const { return m_notePath; }
    QTreeWidget* tree() const { return m_tree; }
    QString branch() const;
    QString status() const;

signals:
    // The note as it was in `commit`; `pathInCommit` is its workdir-relative
    // path there (it may have been renamed since), `workdir` the repository's.
    void versionRequested(const QString& absolutePath, const GitCommit& commit, const QString& workdir);
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
    QList<GitCommit> m_commits;
};

} // namespace coeus
