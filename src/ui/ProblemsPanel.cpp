#include "ui/ProblemsPanel.hpp"

#include "ui/NoteCatalogue.hpp"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;

int rank(const QString& level)
{
    return level == QLatin1String("error") ? 0 : level == QLatin1String("warning") ? 1 : 2;
}

QIcon iconFor(const QString& level)
{
    const QStyle* style = QApplication::style();
    switch (rank(level)) {
    case 0: return style->standardIcon(QStyle::SP_MessageBoxCritical);
    case 1: return style->standardIcon(QStyle::SP_MessageBoxWarning);
    default: return style->standardIcon(QStyle::SP_MessageBoxInformation);
    }
}

QString displayPath(const ProblemRef& problem)
{
    const QString relative = problem.notePath.mid(problem.rootPath.size() + 1);
    return problem.rootName == QLatin1String("vault") ? relative : problem.rootName + QLatin1Char(':') + relative;
}

} // namespace

ProblemsPanel::ProblemsPanel(const NoteCatalogue* catalogue, QWidget* parent)
    : QWidget(parent)
    , m_catalogue(catalogue)
    , m_scope(new QComboBox(this))
    , m_level(new QComboBox(this))
    , m_count(new QLabel(this))
    , m_tree(new QTreeWidget(this))
{
    m_scope->addItem(tr("This project"), int(Scope::Project));
    m_scope->addItem(tr("All projects"), int(Scope::All));
    m_level->addItem(tr("All levels"), 2);
    m_level->addItem(tr("Errors and warnings"), 1);
    m_level->addItem(tr("Errors"), 0);
    m_count->setForegroundRole(QPalette::PlaceholderText);

    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Problem"), tr("Rule"), tr("Line")});
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    auto open = [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, kPathRole).toString();
        if (!path.isEmpty())
            emit openRequested(path, item->data(0, kLineRole).toInt());
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, open);
    connect(m_tree, &QTreeWidget::itemActivated, this, open);

    auto* filters = new QHBoxLayout;
    filters->setContentsMargins(0, 0, 0, 0);
    filters->addWidget(m_scope);
    filters->addWidget(m_level);
    filters->addWidget(m_count, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(filters);
    layout->addWidget(m_tree, 1);

    for (QComboBox* box : {m_scope, m_level})
        connect(box, &QComboBox::currentIndexChanged, this, &ProblemsPanel::present);
    refresh();
}

void ProblemsPanel::refresh()
{
    present();
}

void ProblemsPanel::followNote(const QString& absolutePath)
{
    const auto project = m_catalogue->projectOf(absolutePath);
    if (!project)
        return; // keep the project shown
    const QString folder = project->path.isEmpty() ? project->rootPath : project->rootPath + QLatin1Char('/') + project->path;
    if (folder == m_projectFolder)
        return;
    m_projectFolder = folder;
    present();
}

QString ProblemsPanel::summary() const
{
    return m_count->text();
}

void ProblemsPanel::present()
{
    const bool all = Scope(m_scope->currentData().toInt()) == Scope::All;
    const int worst = m_level->currentData().toInt();
    int counts[3] = {0, 0, 0};

    m_tree->clear();
    QTreeWidgetItem* file = nullptr;
    QString filePath;
    for (const ProblemRef& problem : m_catalogue->problems()) {
        if (!all) {
            // Problems of the followed project, not of projects nested in it.
            const auto project = m_catalogue->projectOf(problem.notePath);
            const QString folder = !project ? QString()
                                   : project->path.isEmpty() ? project->rootPath
                                                             : project->rootPath + QLatin1Char('/') + project->path;
            if (m_projectFolder.isEmpty() || folder != m_projectFolder)
                continue;
        }
        if (rank(problem.level) > worst)
            continue;
        ++counts[rank(problem.level)];
        if (!file || problem.notePath != filePath) {
            filePath = problem.notePath;
            file = new QTreeWidgetItem(m_tree, {displayPath(problem)});
            file->setData(0, kPathRole, problem.notePath);
            file->setData(0, kLineRole, 1);
            file->setFirstColumnSpanned(true);
            file->setExpanded(true);
        }
        auto* item = new QTreeWidgetItem(file, {problem.message, problem.rule, QString::number(problem.line)});
        item->setIcon(0, iconFor(problem.level));
        item->setToolTip(0, problem.message);
        item->setData(0, kPathRole, problem.notePath);
        item->setData(0, kLineRole, problem.line);
        item->setForeground(1, palette().brush(QPalette::PlaceholderText));
        item->setForeground(2, palette().brush(QPalette::PlaceholderText));
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = m_tree->topLevelItem(i);
        top->setText(0, QStringLiteral("%1 (%2)").arg(top->text(0)).arg(top->childCount()));
    }

    if (!all && m_projectFolder.isEmpty()) {
        m_count->setText(tr("Open a note in a project, or choose All projects"));
        return;
    }
    QStringList parts;
    parts.append(counts[0] == 1 ? tr("1 error") : tr("%1 errors").arg(counts[0]));
    parts.append(counts[1] == 1 ? tr("1 warning") : tr("%1 warnings").arg(counts[1]));
    parts.append(tr("%1 info").arg(counts[2]));
    m_count->setText(parts.join(QStringLiteral(" · ")));
}

} // namespace coeus
