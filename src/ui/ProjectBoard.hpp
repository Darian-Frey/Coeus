#pragma once

#include "git/GitRepository.hpp"
#include "index/IndexQueries.hpp"
#include "registers/ProjectStatus.hpp"

#include <QDate>
#include <QWidget>

class QLabel;
class QThreadPool;
class QTreeWidget;
class QTreeWidgetItem;

namespace coeus {

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

    struct ProjectRow {
        ProjectRef project;
        QString folder;   // absolute
        QString readme;   // absolute
        int reviewedDaysAgo = -1; // -1: not a date
        ProjectStatus::Build build;
        bool inGit = false;
        QString branch;
        std::optional<GitRepository::Tracking> tracking;
        int changes = 0;  // changed files under the project's folder
    };

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

signals:
    void openRequested(const QString& absolutePath, int line);
    void updated(int projects, int ideas);

private:
    void present(const QList<ProjectRow>& projects, const QList<IdeaRef>& ideas, int threshold);

    const NoteCatalogue* m_catalogue;
    QThreadPool* m_pool;
    QDate m_today;
    int m_generation = 0;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_tree = nullptr;
};

} // namespace coeus
