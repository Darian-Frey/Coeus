#include "ui/ProjectBoard.hpp"

#include "git/GitCommand.hpp"
#include "registers/Lint.hpp"
#include "registers/Vocabulary.hpp"
#include "ui/NoteCatalogue.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QThreadPool>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIdeaRole = Qt::UserRole + 2; // index into m_ideas, on idea rows
const QColor kStale(0xc0, 0x7a, 0x00);

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
    , m_fetch(new QPushButton(tr("Fetch"), this))
    , m_tree(new QTreeWidget(this))
    , m_menu(new QMenu(this))
    , m_fetcher(new GitCommand(this))
{
    m_fetch->setToolTip(tr("Run git fetch in every project's repository, so ahead and behind are current"));
    connect(m_fetch, &QPushButton::clicked, this, &ProjectBoard::fetchAll);
    connect(m_fetcher, &GitCommand::finished, this, [this](bool ok, const QString& message) {
        if (!ok)
            m_fetchFailures.append(QStringLiteral("%1: %2").arg(QFileInfo(m_fetchQueue.front()).fileName(), message));
        m_fetchQueue.removeFirst();
        fetchNext();
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        if (QMenu* menu = rowMenu(m_tree->itemAt(at)); !menu->isEmpty())
            menu->popup(m_tree->viewport()->mapToGlobal(at));
    });
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

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(m_summary, 1);
    header->addWidget(m_fetch);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(header);
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

QString ProjectBoard::summary() const
{
    return m_summary->text();
}

QMenu* ProjectBoard::rowMenu(QTreeWidgetItem* item)
{
    m_menu->clear();
    if (!item || !item->parent())
        return m_menu; // a group heading
    const QString path = item->data(Name, kPathRole).toString();
    const QVariant idea = item->data(Name, kIdeaRole);
    if (!idea.isValid()) {
        const QString name = item->text(Name);
        m_menu->addAction(tr("Open README"), this, [this, path] { emit openRequested(path, 1); });
        m_menu->addAction(tr("Refresh Header…"), this, [this, path, name] { emit refreshHeaderRequested(path, name); });
    } else {
        const int index = idea.toInt();
        m_menu->addAction(tr("Open"), this, [this, path] { emit openRequested(path, 1); });
        m_menu->addAction(tr("Promote Idea…"), this, [this, index] {
            if (index >= 0 && index < m_ideas.size())
                emit promoteRequested(m_ideas[index]);
        });
    }
    return m_menu;
}

bool ProjectBoard::isFetching() const
{
    return !m_fetchQueue.isEmpty();
}

void ProjectBoard::fetchAll()
{
    if (isFetching())
        return;
    for (const ProjectRow& row : std::as_const(m_rows)) {
        if (row.inGit && !m_fetchQueue.contains(row.workdir))
            m_fetchQueue.append(row.workdir);
    }
    m_fetchTotal = int(m_fetchQueue.size());
    m_fetchFailures.clear();
    if (m_fetchQueue.isEmpty()) {
        m_summary->setText(tr("No project is in a Git repository: nothing to fetch"));
        emit fetched(0, {});
        return;
    }
    m_fetch->setEnabled(false);
    fetchNext();
}

void ProjectBoard::fetchNext()
{
    if (m_fetchQueue.isEmpty()) {
        m_fetch->setEnabled(true);
        refresh(); // ahead/behind as of now
        const QStringList failures = m_fetchFailures;
        if (!failures.isEmpty())
            m_fetch->setToolTip(tr("Last fetch: %1").arg(failures.join(QStringLiteral("; "))));
        emit fetched(m_fetchTotal, failures);
        return;
    }
    const QString workdir = m_fetchQueue.front();
    m_summary->setText(tr("Fetching %1 of %2: %3…").arg(m_fetchTotal - m_fetchQueue.size() + 1).arg(m_fetchTotal)
                           .arg(QFileInfo(workdir).fileName()));
    if (!m_fetcher->start(workdir, {QStringLiteral("fetch"), QStringLiteral("--quiet")})) {
        m_fetchFailures.append(QStringLiteral("%1: a fetch is already running").arg(QFileInfo(workdir).fileName()));
        m_fetchQueue.removeFirst();
        fetchNext();
    }
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
        for (const ProjectRef& project : projects)
            rows.append(ProjectOverview::of(project, today));
        return rows;
    }));
}

void ProjectBoard::present(const QList<ProjectRow>& unsorted, const QList<IdeaRef>& unsortedIdeas, int threshold)
{
    QList<ProjectRow> projects = unsorted;
    std::stable_sort(projects.begin(), projects.end(), [](const ProjectRow& a, const ProjectRow& b) {
        const int x = orderOf(Vocabulary::projectStatus(), a.project.status);
        const int y = orderOf(Vocabulary::projectStatus(), b.project.status);
        return x != y ? x < y : a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
    QList<IdeaRef> ideas = unsortedIdeas;
    std::stable_sort(ideas.begin(), ideas.end(), [](const IdeaRef& a, const IdeaRef& b) {
        const int x = orderOf(Vocabulary::ideaStage(), a.stage);
        const int y = orderOf(Vocabulary::ideaStage(), b.stage);
        return x != y ? x < y : ideaName(a).compare(ideaName(b), Qt::CaseInsensitive) < 0;
    });

    m_rows = projects;
    m_ideas = ideas;
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
        item->setText(Name, row.label);
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
        item->setData(Name, kIdeaRole, int(&idea - ideas.constData()));
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
