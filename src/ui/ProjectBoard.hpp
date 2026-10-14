#pragma once

#include "git/GitRepository.hpp"
#include "index/IndexQueries.hpp"
#include "registers/ProjectOverview.hpp"

#include <QDate>
#include <QWidget>

class QLabel;
class QMenu;
class QPushButton;
class QThreadPool;
class QTreeWidget;
class QTreeWidgetItem;

namespace coeus {

class GitCommand;
class NoteCatalogue;

// The project board (F-032, absorbing F-017): every project by README
// status, with how long since it was reviewed, where its ROADMAP stands,
// what its CHANGELOG has not released, and its Git branch, changes and
// ahead/behind; then every idea by stage. Nothing on it is kept by hand.
// Projects and ideas come from the catalogue; the documents and Git are read
// on the Git worker (`pool`), never the UI thread (AV-005).
class ProjectBoard : public QWidget {
    Q_OBJECT

public:
    enum Column { Name, Status, Reviewed, Build, Unreleased, Git, ColumnCount };

    using ProjectRow = ProjectOverview;

    ProjectBoard(const NoteCatalogue* catalogue, QThreadPool* pool, QWidget* parent = nullptr);

    // Reads everything again from the catalogue's snapshot, documents and Git.
    void refresh();
    // For tests: the date ages are measured from (today by default).
    void setToday(const QDate& today) { m_today = today; }

    QTreeWidget* tree() const { return m_tree; }
    QTreeWidgetItem* projectsGroup() const;
    QTreeWidgetItem* ideasGroup() const;
    // The row named `name` (a project's label or an idea's name), or nullptr.
    QTreeWidgetItem* row(const QString& name) const;
    // The menu a right-click on `item` shows (for tests too): a project's
    // Open README and Refresh Header…, an idea's Open and Promote Idea….
    QMenu* rowMenu(QTreeWidgetItem* item);

    // Runs `git fetch` in every project's repository, one after another
    // (D-020), then reads the board again. Only on request; never automatic.
    void fetchAll();
    bool isFetching() const;
    QPushButton* fetchButton() const { return m_fetch; }
    QString summary() const;

signals:
    void openRequested(const QString& absolutePath, int line);
    void updated(int projects, int ideas);
    // F-032 commands, carried out by MainWindow after confirmation.
    void refreshHeaderRequested(const QString& readmePath, const QString& projectLabel);
    void promoteRequested(const coeus::IdeaRef& idea);
    // Every repository fetched, or tried: how many, and the failures as "name: reason".
    void fetched(int repositories, const QStringList& failures);

private:
    void present(const QList<ProjectRow>& projects, const QList<IdeaRef>& ideas, int threshold);
    void fetchNext();

    const NoteCatalogue* m_catalogue;
    QThreadPool* m_pool;
    QDate m_today;
    int m_generation = 0;
    QLabel* m_summary = nullptr;
    QPushButton* m_fetch = nullptr;
    QTreeWidget* m_tree = nullptr;
    QMenu* m_menu = nullptr;
    QList<ProjectRow> m_rows;
    QList<IdeaRef> m_ideas;
    GitCommand* m_fetcher = nullptr;
    QStringList m_fetchQueue;
    int m_fetchTotal = 0;
    QStringList m_fetchFailures;
};

} // namespace coeus
