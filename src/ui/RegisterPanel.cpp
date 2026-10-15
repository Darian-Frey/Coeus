#include "ui/RegisterPanel.hpp"

#include "registers/RegisterTemplates.hpp"
#include "ui/FuzzyMatch.hpp"
#include "ui/NoteCatalogue.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;
constexpr int kComboChars = 14; // a project or status filter shows this much, then elides

// "projects/coeus" in the vault, "pontus" or "pontus/sub" in a linked root.
QString projectLabel(const ProjectRef& project)
{
    if (project.rootName == QLatin1String("vault"))
        return project.path.isEmpty() ? QStringLiteral("(vault)") : project.path;
    return project.path.isEmpty() ? project.rootName : project.rootName + QLatin1Char('/') + project.path;
}

QString projectKey(const QString& rootPath, const QString& path)
{
    return rootPath + QLatin1Char('\x1f') + path;
}

} // namespace

RegisterPanel::RegisterPanel(const NoteCatalogue* catalogue, QWidget* parent)
    : QWidget(parent)
    , m_catalogue(catalogue)
    , m_project(new QComboBox(this))
    , m_kind(new QComboBox(this))
    , m_status(new QComboBox(this))
    , m_filter(new QLineEdit(this))
    , m_count(new QLabel(this))
    , m_tree(new QTreeWidget(this))
{
    // Filled from the registers, whose statuses can run to a paragraph: sized
    // to a few words, never to the longest item, which a restyle would
    // otherwise measure again and widen the window by (BUG-009).
    for (QComboBox* box : {m_project, m_status}) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(kComboChars);
    }
    m_kind->addItem(tr("All kinds"), QString());
    for (const QString& kind : RegisterTemplates::kinds())
        m_kind->addItem(QStringLiteral("%1 — %2").arg(kind, RegisterTemplates::displayName(kind)), kind);
    m_filter->setPlaceholderText(tr("Filter by ID or title…"));
    m_filter->setClearButtonEnabled(true);
    m_count->setForegroundRole(QPalette::PlaceholderText);

    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("ID"), tr("Title"), tr("Status")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    auto open = [this](QTreeWidgetItem* item) {
        emit openRequested(item->data(0, kPathRole).toString(), item->data(0, kLineRole).toInt());
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, open);
    connect(m_tree, &QTreeWidget::itemActivated, this, open);

    auto* filters = new QHBoxLayout;
    filters->setContentsMargins(0, 0, 0, 0);
    filters->addWidget(m_kind, 1);
    filters->addWidget(m_status, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_project);
    layout->addLayout(filters);
    layout->addWidget(m_filter);
    layout->addWidget(m_count);
    layout->addWidget(m_tree, 1);

    for (QComboBox* box : {m_project, m_kind, m_status})
        connect(box, &QComboBox::currentIndexChanged, this, [this] {
            if (!m_updating)
                present();
        });
    connect(m_filter, &QLineEdit::textChanged, this, &RegisterPanel::present);
    refresh();
}

void RegisterPanel::refresh()
{
    // Keep the chosen project across reloads.
    const QString current = m_project->currentData().toString();
    {
        const QSignalBlocker blocker(m_project);
        m_project->clear();
        for (const ProjectRef& project : m_catalogue->projects())
            m_project->addItem(projectLabel(project), projectKey(project.rootPath, project.path));
        const int index = m_project->findData(current);
        m_project->setCurrentIndex(index >= 0 ? index : 0);
    }
    present();
}

void RegisterPanel::followNote(const QString& absolutePath)
{
    const auto project = m_catalogue->projectOf(absolutePath);
    if (!project)
        return; // leave the panel where it was
    const int index = m_project->findData(projectKey(project->rootPath, project->path));
    if (index >= 0 && index != m_project->currentIndex())
        m_project->setCurrentIndex(index);
}

QList<RegisterRef> RegisterPanel::entriesOfProject() const
{
    QList<RegisterRef> entries;
    const QString key = m_project->currentData().toString();
    for (const RegisterRef& entry : m_catalogue->registers()) {
        if (projectKey(entry.rootPath, entry.project) == key)
            entries.append(entry);
    }
    return entries;
}

void RegisterPanel::present()
{
    const QList<RegisterRef> entries = entriesOfProject();
    const QString kind = m_kind->currentData().toString();

    // Status choices: those present for the chosen kind, as written.
    {
        m_updating = true;
        const QString chosen = m_status->currentData().toString();
        m_status->clear();
        m_status->addItem(tr("All statuses"), QString());
        QStringList statuses;
        for (const RegisterRef& entry : entries) {
            if ((kind.isEmpty() || entry.kind == kind) && !entry.status.isEmpty() && !statuses.contains(entry.status))
                statuses.append(entry.status);
        }
        statuses.sort(Qt::CaseInsensitive);
        for (const QString& status : std::as_const(statuses)) {
            m_status->addItem(status, status);
            m_status->setItemData(m_status->count() - 1, status, Qt::ToolTipRole); // the whole of an elided one
        }
        const int index = m_status->findData(chosen);
        m_status->setCurrentIndex(index >= 0 ? index : 0);
        m_updating = false;
    }

    const QString status = m_status->currentData().toString();
    const QString text = m_filter->text().trimmed();
    m_tree->clear();
    int shown = 0;
    for (const RegisterRef& entry : entries) {
        if (!kind.isEmpty() && entry.kind != kind)
            continue;
        if (!status.isEmpty() && entry.status != status)
            continue;
        if (!text.isEmpty() && !fuzzyScore(text, entry.ident + QLatin1Char(' ') + entry.title))
            continue;
        auto* item = new QTreeWidgetItem(m_tree, {entry.ident, entry.title, entry.status});
        item->setData(0, kPathRole, entry.notePath);
        item->setData(0, kLineRole, entry.line);
        item->setToolTip(1, entry.title);
        item->setForeground(2, palette().brush(QPalette::PlaceholderText));
        ++shown;
    }
    if (m_project->count() == 0)
        m_count->setText(tr("No projects yet: a project is a folder whose README.md opens with the status header."));
    else
        m_count->setText(shown == 1 ? tr("1 entry") : tr("%1 entries").arg(shown));
}

} // namespace coeus
