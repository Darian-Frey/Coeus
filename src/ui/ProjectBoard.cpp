#include "ui/ProjectBoard.hpp"

#include "registers/Lint.hpp"
#include "registers/Vocabulary.hpp"
#include "ui/NoteCatalogue.hpp"

#include <QFile>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QThreadPool>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
const QColor kStale(0xc0, 0x7a, 0x00);

std::optional<QString> slurp(const std::optional<QString>& path)
{
    if (!path)
        return std::nullopt;
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

QString label(const ProjectRef& project)
{
    if (project.rootName == QLatin1String("vault"))
        return project.path.isEmpty() ? QStringLiteral("(vault)") : project.path;
    return project.path.isEmpty() ? project.rootName : project.rootName + QLatin1Char('/') + project.path;
}

int orderOf(const QStringList& vocabulary, const QString& value)
{
    const auto term = Vocabulary::termOf(vocabulary, value);
    return term ? int(vocabulary.indexOf(*term)) : int(vocabulary.size());
}

QString ideaName(const IdeaRef& idea)
{
    return idea.codename.isEmpty() ? idea.title : idea.codename;
}

} // namespace

ProjectBoard::ProjectBoard(const NoteCatalogue* catalogue, QThreadPool* pool, QWidget* parent)
    : QWidget(parent)
    , m_catalogue(catalogue)
    , m_pool(pool)
    , m_today(QDate::currentDate())
    , m_summary(new QLabel(this))
    , m_tree(new QTreeWidget(this))
{
    m_summary->setForegroundRole(QPalette::PlaceholderText);
    m_tree->setColumnCount(ColumnCount);
    m_tree->setHeaderLabels({tr("Name"), tr("Status"), tr("Reviewed"), tr("Build"), tr("Unreleased"), tr("Git")});
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    for (int column : {Name, Status, Reviewed, Unreleased, Git})
        m_tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Build, QHeaderView::Stretch);
    auto open = [this](QTreeWidgetItem* item) {
        const QString path = item->data(Name, kPathRole).toString();
        if (!path.isEmpty())
            emit openRequested(path, 1);
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, open);
    connect(m_tree, &QTreeWidget::itemActivated, this, open);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_summary);
    layout->addWidget(m_tree, 1);
    present({}, {}, Lint::kDefaultReviewThresholdDays);
}

QTreeWidgetItem* ProjectBoard::projectsGroup() const
{
    return m_tree->topLevelItem(0);
}

QTreeWidgetItem* ProjectBoard::ideasGroup() const
{
    return m_tree->topLevelItem(1);
}

QTreeWidgetItem* ProjectBoard::row(const QString& name) const
{
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            if (group->child(i)->text(Name) == name)
                return group->child(i);
        }
    }
    return nullptr;
}

void ProjectBoard::refresh()
{
    const int generation = ++m_generation;
    const QList<ProjectRef> projects = m_catalogue->projects();
    const QList<IdeaRef> ideas = m_catalogue->ideas();
    const int threshold = m_catalogue->reviewThreshold();
    const QDate today = m_today;
    auto* watcher = new QFutureWatcher<QList<ProjectRow>>(this);
    connect(watcher, &QFutureWatcher<QList<ProjectRow>>::finished, this, [this, watcher, generation, ideas, threshold] {
        watcher->deleteLater();
        if (generation == m_generation)
            present(watcher->result(), ideas, threshold);
    });
    watcher->setFuture(QtConcurrent::run(m_pool, [projects, today] {
        QList<ProjectRow> rows;
        for (const ProjectRef& project : projects) {
            ProjectRow row;
            row.project = project;
            row.folder = project.path.isEmpty() ? project.rootPath : project.rootPath + QLatin1Char('/') + project.path;
            row.readme = row.folder + QStringLiteral("/README.md");
            const QDate reviewed = QDate::fromString(project.lastReviewed.left(10), Qt::ISODate);
            row.reviewedDaysAgo = reviewed.isValid() ? int(reviewed.daysTo(today)) : -1;
            row.build = ProjectStatus::parse(slurp(ProjectStatus::find(row.folder, QStringLiteral("ROADMAP.md"))),
                                             slurp(ProjectStatus::find(row.folder, QStringLiteral("CHANGELOG.md"))));
            if (const auto repo = GitRepository::discover(row.folder)) {
                row.inGit = true;
                row.branch = repo->branch();
                row.tracking = repo->tracking();
                const QString prefix = repo->relativePath(row.folder);
                if (const auto changes = repo->status()) {
                    for (const GitChange& change : *changes) {
                        if (prefix == QLatin1String(".") || prefix.isEmpty() || change.path.startsWith(prefix + QLatin1Char('/')))
                            ++row.changes;
                    }
                }
            }
            rows.append(row);
        }
        return rows;
    }));
}

void ProjectBoard::present(const QList<ProjectRow>& unsorted, const QList<IdeaRef>& unsortedIdeas, int threshold)
{
    QList<ProjectRow> projects = unsorted;
    std::stable_sort(projects.begin(), projects.end(), [](const ProjectRow& a, const ProjectRow& b) {
        const int x = orderOf(Vocabulary::projectStatus(), a.project.status);
        const int y = orderOf(Vocabulary::projectStatus(), b.project.status);
        return x != y ? x < y : label(a.project).compare(label(b.project), Qt::CaseInsensitive) < 0;
    });
    QList<IdeaRef> ideas = unsortedIdeas;
    std::stable_sort(ideas.begin(), ideas.end(), [](const IdeaRef& a, const IdeaRef& b) {
        const int x = orderOf(Vocabulary::ideaStage(), a.stage);
        const int y = orderOf(Vocabulary::ideaStage(), b.stage);
        return x != y ? x < y : ideaName(a).compare(ideaName(b), Qt::CaseInsensitive) < 0;
    });

    m_tree->clear();
    auto* projectsGroup = new QTreeWidgetItem(m_tree, {tr("Projects (%1)").arg(projects.size())});
    auto* ideasGroup = new QTreeWidgetItem(m_tree, {tr("Ideas (%1)").arg(ideas.size())});
    for (QTreeWidgetItem* group : {projectsGroup, ideasGroup}) {
        group->setFirstColumnSpanned(true);
        QFont font = group->font(Name);
        font.setBold(true);
        group->setFont(Name, font);
    }
    const QBrush muted = palette().brush(QPalette::PlaceholderText);
    int stale = 0;
    for (const ProjectRow& row : std::as_const(projects)) {
        auto* item = new QTreeWidgetItem(projectsGroup);
        item->setText(Name, label(row.project));
        item->setData(Name, kPathRole, row.readme);
        item->setToolTip(Name, row.folder);
        item->setText(Status, row.project.status.section(QLatin1Char(' '), 0, 0));
        item->setToolTip(Status, row.project.status);

        // Freshness (F-017): days since Last reviewed, amber past the threshold.
        if (row.reviewedDaysAgo < 0) {
            item->setText(Reviewed, tr("no date"));
            item->setForeground(Reviewed, QBrush(kStale));
        } else {
            item->setText(Reviewed, row.reviewedDaysAgo == 0 ? tr("today") : tr("%1 d").arg(row.reviewedDaysAgo));
            if (row.reviewedDaysAgo > threshold) {
                item->setForeground(Reviewed, QBrush(kStale));
                ++stale;
            }
        }
        item->setToolTip(Reviewed, tr("Last reviewed %1; stale after %2 days").arg(row.project.lastReviewed).arg(threshold));

        const QString build = row.build.summary();
        item->setText(Build, build.isEmpty() ? tr("no ROADMAP") : build);
        if (build.isEmpty())
            item->setForeground(Build, muted);
        item->setToolTip(Build, row.build.phaseStatus.isEmpty() ? build : row.build.phaseStatus);

        if (!row.build.hasChangelog) {
            item->setText(Unreleased, QStringLiteral("—"));
            item->setForeground(Unreleased, muted);
        } else {
            item->setText(Unreleased, QString::number(row.build.unreleased));
            item->setToolTip(Unreleased, row.build.lastRelease.isEmpty()
                                             ? tr("%n change(s) under [Unreleased]; nothing released yet", nullptr, row.build.unreleased)
                                             : tr("%n change(s) since %1 (%2)", nullptr, row.build.unreleased)
                                                   .arg(row.build.lastRelease, row.build.lastReleaseDate));
        }

        if (!row.inGit) {
            item->setText(Git, tr("not in Git"));
            item->setForeground(Git, muted);
        } else {
            QString git = row.branch;
            if (row.tracking)
                git += QStringLiteral(" ↑%1 ↓%2").arg(row.tracking->ahead).arg(row.tracking->behind);
            if (row.changes > 0)
                git += QStringLiteral(" · ●%1").arg(row.changes);
            item->setText(Git, git);
            item->setToolTip(Git, (row.tracking ? tr("%1 ahead, %2 behind %3 as of the last fetch")
                                                      .arg(row.tracking->ahead)
                                                      .arg(row.tracking->behind)
                                                      .arg(row.tracking->upstream)
                                                : tr("No upstream branch"))
                                      + tr("; %n file(s) changed in this project", nullptr, row.changes));
        }
    }
    for (const IdeaRef& idea : std::as_const(ideas)) {
        auto* item = new QTreeWidgetItem(ideasGroup);
        item->setText(Name, ideaName(idea));
        item->setData(Name, kPathRole, idea.notePath);
        item->setToolTip(Name, idea.notePath);
        item->setText(Status, idea.stage);
        if (!Vocabulary::termOf(Vocabulary::ideaStage(), idea.stage))
            item->setForeground(Status, QBrush(kStale));
        item->setText(Build, idea.summary);
        item->setText(Git, idea.repo);
        item->setForeground(Git, muted);
    }
    projectsGroup->setExpanded(true);
    ideasGroup->setExpanded(true);
    m_summary->setText(stale == 0 ? tr("%n project(s), all reviewed within %1 days", nullptr, int(projects.size())).arg(threshold)
                                  : tr("%n project(s), %1 not reviewed for over %2 days", nullptr, int(projects.size()))
                                        .arg(stale)
                                        .arg(threshold));
    emit updated(int(projects.size()), int(ideas.size()));
}

} // namespace coeus
