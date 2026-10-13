#include "ui/TagPanel.hpp"

#include "ui/FuzzyMatch.hpp"

#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

constexpr int kTagRole = Qt::UserRole + 1;

// True if the node or anything beneath it matches the filter text.
bool matches(const Tags::Node& node, const QString& filter)
{
    if (filter.isEmpty() || fuzzyScore(filter, node.fullTag))
        return true;
    return std::any_of(node.children.begin(), node.children.end(),
                       [&filter](const Tags::Node& child) { return matches(child, filter); });
}

} // namespace

TagPanel::TagPanel(QWidget* parent)
    : QWidget(parent)
    , m_filter(new QLineEdit(this))
    , m_empty(new QLabel(this))
    , m_tree(new QTreeWidget(this))
{
    m_filter->setPlaceholderText(tr("Filter tags…"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &TagPanel::present);

    m_empty->setForegroundRole(QPalette::PlaceholderText);
    m_empty->setWordWrap(true);
    m_empty->setMargin(6);

    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(14);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item) { emit tagActivated(item->data(0, kTagRole).toString()); });
    connect(m_tree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item) { emit tagActivated(item->data(0, kTagRole).toString()); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_filter);
    layout->addWidget(m_empty);
    layout->addWidget(m_tree, 1);
    present();
}

void TagPanel::setIndexPath(const QString& indexPath)
{
    m_indexPath = indexPath;
    m_usages.clear();
    ++m_generation;
    present();
}

void TagPanel::reload()
{
    if (m_indexPath.isEmpty())
        return;
    const int generation = ++m_generation;
    auto* watcher = new QFutureWatcher<QList<Tags::Usage>>(this);
    connect(watcher, &QFutureWatcher<QList<Tags::Usage>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_usages = watcher->result();
        present();
    });
    watcher->setFuture(QtConcurrent::run([path = m_indexPath] { return Tags::load(path).value_or(QList<Tags::Usage>()); }));
}

QList<Tags::Usage> TagPanel::notesFor(const QString& tag) const
{
    return Tags::notesFor(m_usages, tag);
}

void TagPanel::addNodes(QTreeWidgetItem* parent, const QList<Tags::Node>& nodes)
{
    const QString filter = m_filter->text().trimmed();
    for (const Tags::Node& node : nodes) {
        if (!matches(node, filter))
            continue;
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, QLatin1Char('#') + node.name);
        item->setText(1, QString::number(node.noteCount));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(1, palette().brush(QPalette::PlaceholderText));
        item->setToolTip(0, tr("#%1 · %n note(s)", nullptr, node.noteCount).arg(node.fullTag));
        item->setData(0, kTagRole, node.fullTag);
        addNodes(item, node.children);
    }
}

void TagPanel::present()
{
    m_tree->clear();
    const QList<Tags::Node> nodes = Tags::tree(m_usages);
    addNodes(nullptr, nodes);
    if (!m_filter->text().trimmed().isEmpty())
        m_tree->expandAll();
    const bool none = m_tree->topLevelItemCount() == 0;
    m_empty->setText(m_indexPath.isEmpty() ? tr("Open a vault to see its tags.")
                     : m_usages.isEmpty()  ? tr("No tags yet. Write #tag in a note, or tags: in its frontmatter.")
                                           : tr("No tags match."));
    m_empty->setVisible(none);
    m_tree->setVisible(!none);
    emit reloaded(int(nodes.size()));
}

} // namespace coeus
