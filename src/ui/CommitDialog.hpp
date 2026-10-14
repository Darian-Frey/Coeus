#pragma once

#include "git/GitRepository.hpp"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

namespace coeus {

// Committing from Coeus (F-022): the repository's changes with a tick for
// each, a multi-paragraph message, and whether to push afterwards. Not
// modal; it asks MainWindow to do the work (commitRequested) and shows the
// outcome, so libgit2 and git never run on the UI thread (AV-005).
class CommitDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommitDialog(QWidget* parent = nullptr);

    // Fills the dialog for one repository. Tracked changes and new notes are
    // ticked; other untracked files are listed unticked. `unsaved` names open
    // notes in it with unsaved changes, which a commit would not include.
    void setRepository(const QString& workdir, const QString& branch, const QList<GitChange>& changes,
                       const QStringList& unsaved);
    void setBusy(bool busy, const QString& what = QString());
    void showError(const QString& message);

    QString workdir() const { return m_workdir; }
    QStringList selectedPaths() const;
    QString message() const;
    QTreeWidget* tree() const { return m_tree; }
    QPlainTextEdit* messageEdit() const { return m_message; }
    QCheckBox* pushBox() const { return m_push; }
    QPushButton* commitButton() const { return m_commit; }
    QPushButton* saveAllButton() const { return m_saveAll; }
    QLabel* errorLabel() const { return m_error; }

signals:
    void commitRequested(const QString& workdir, const QStringList& paths, const QString& message, bool push);
    void saveAllRequested(const QString& workdir);

private:
    void updateCommitButton();

    QString m_workdir;
    QLabel* m_heading = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_unsaved = nullptr;
    QPushButton* m_saveAll = nullptr;
    QPlainTextEdit* m_message = nullptr;
    QCheckBox* m_push = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_commit = nullptr;
    QPushButton* m_cancel = nullptr;
};

} // namespace coeus
