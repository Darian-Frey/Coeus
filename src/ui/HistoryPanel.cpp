#include "ui/HistoryPanel.hpp"

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QThreadPool>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

constexpr int kIndexRole = Qt::UserRole + 1;
constexpr int kHistoryLimit = 500;

} // namespace

HistoryPanel::HistoryPanel(QThreadPool* pool, QWidget* parent)
    : QWidget(parent)
    , m_pool(pool)
    , m_branch(new QLabel(this))
    , m_status(new QLabel(this))
    , m_refresh(new QToolButton(this))
    , m_tree(new QTreeWidget(this))
{
    m_branch->setForegroundRole(QPalette::PlaceholderText);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_status->setWordWrap(true);
    m_refresh->setText(QStringLiteral("↻"));
    m_refresh->setAutoRaise(true);
    m_refresh->setToolTip(tr("Read the history again"));
    connect(m_refresh, &QToolButton::clicked, this, &HistoryPanel::refresh);

    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setWordWrap(false); // summary and "date · author" each elide on their own line
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->setMinimumHeight(80);
    auto open = [this](QTreeWidgetItem* item) {
        const int index = item->data(0, kIndexRole).toInt();
        if (index >= 0 && index < m_commits.size())
            emit versionRequested(m_notePath, m_commits[index], m_workdir);
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, open);
    connect(m_tree, &QTreeWidget::itemActivated, this, open);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(m_branch, 1);
    header->addWidget(m_refresh);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(header);
    layout->addWidget(m_status);
    layout->addWidget(m_tree);
    showNote(QString());
}

QString HistoryPanel::branch() const
{
    return m_branch->text();
}

QString HistoryPanel::status() const
{
    return m_status->text();
}

void HistoryPanel::showNote(const QString& absolutePath)
{
    m_notePath = absolutePath;
    refresh();
}

void HistoryPanel::refresh()
{
    const int generation = ++m_generation;
    if (m_notePath.isEmpty()) {
        present({tr("Open a note to see its history."), {}, {}, {}});
        return;
    }
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_generation)
            present(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(m_pool, [path = m_notePath] {
        Result result;
        const auto repo = GitRepository::discover(path);
        if (!repo) {
            result.error = repo.error();
            return result;
        }
        result.workdir = repo->workdir();
        result.branch = repo->branch();
        const auto commits = repo->fileHistory(repo->relativePath(path), kHistoryLimit);
        if (!commits)
            result.error = commits.error();
        else
            result.commits = *commits;
        return result;
    }));
}

void HistoryPanel::present(const Result& result)
{
    m_workdir = result.workdir;
    m_commits = result.commits;
    m_branch->setText(result.branch);
    m_branch->setToolTip(result.workdir);
    m_tree->clear();
    if (!result.error.isEmpty())
        m_status->setText(result.error);
    else if (m_commits.isEmpty())
        m_status->setText(tr("No commits of this note yet."));
    else
        m_status->setText(m_commits.size() == 1 ? tr("1 commit") : tr("%1 commits").arg(m_commits.size()));
    m_tree->setVisible(!m_commits.isEmpty());

    const QFont mono = QFont(QStringLiteral("monospace"));
    for (qsizetype i = 0; i < m_commits.size(); ++i) {
        const GitCommit& commit = m_commits[i];
        const QString when = QLocale().toString(commit.time.date(), QStringLiteral("yyyy-MM-dd"));
        auto* item = new QTreeWidgetItem(m_tree, {commit.shortId, commit.summary + QLatin1Char('\n') + when
                                                                       + QStringLiteral(" · ") + commit.author});
        item->setData(0, kIndexRole, int(i));
        item->setFont(0, mono);
        item->setForeground(0, palette().brush(QPalette::Link));
        item->setToolTip(1, QStringLiteral("%1\n\n%2 · %3 <%4>%5")
                                .arg(commit.message, commit.time.toString(Qt::ISODate), commit.author, commit.email,
                                     commit.path == m_commits.front().path ? QString()
                                                                           : tr("\nThen named %1").arg(commit.path)));
    }
    emit updated(int(m_commits.size()));
}

} // namespace coeus
