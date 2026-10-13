#include "ui/BacklinksPanel.hpp"

#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace coeus {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;

QTreeWidget* makeTree(QWidget* parent)
{
    auto* tree = new QTreeWidget(parent);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setWordWrap(true);
    tree->setFrameShape(QFrame::NoFrame);
    tree->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setTextElideMode(Qt::ElideRight);
    return tree;
}

} // namespace

BacklinksPanel::BacklinksPanel(QWidget* parent)
    : QWidget(parent)
    , m_linksLabel(new QLabel(this))
    , m_links(makeTree(this))
    , m_mentionsHeader(new QToolButton(this))
    , m_mentions(makeTree(this))
{
    m_linksLabel->setForegroundRole(QPalette::PlaceholderText);
    m_linksLabel->setWordWrap(true);

    m_mentionsHeader->setCheckable(true);
    m_mentionsHeader->setChecked(true);
    m_mentionsHeader->setAutoRaise(true);
    m_mentionsHeader->setArrowType(Qt::DownArrow);
    m_mentionsHeader->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_mentionsHeader, &QToolButton::toggled, this, [this](bool open) {
        m_mentions->setVisible(open);
        m_mentionsHeader->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_linksLabel);
    layout->addWidget(m_links);
    layout->addWidget(m_mentionsHeader);
    layout->addWidget(m_mentions);

    for (QTreeWidget* tree : {m_links, m_mentions}) {
        connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
            emit openRequested(item->data(0, kPathRole).toString(), item->data(0, kLineRole).toInt());
        });
        connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
            emit openRequested(item->data(0, kPathRole).toString(), item->data(0, kLineRole).toInt());
        });
    }
    present(Backlinks::Result{});
}

void BacklinksPanel::setIndexPath(const QString& indexPath)
{
    m_indexPath = indexPath;
    m_notePath.clear();
    ++m_generation;
    present(Backlinks::Result{});
}

void BacklinksPanel::showNote(const QString& notePath)
{
    if (notePath == m_notePath)
        return;
    m_notePath = notePath;
    refresh();
}

void BacklinksPanel::refresh()
{
    const int generation = ++m_generation;
    if (m_notePath.isEmpty() || m_indexPath.isEmpty()) {
        present(Backlinks::Result{});
        return;
    }
    auto* watcher = new QFutureWatcher<Backlinks::Result>(this);
    connect(watcher, &QFutureWatcher<Backlinks::Result>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_generation) // a newer note or index change superseded this
            present(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([index = m_indexPath, note = m_notePath] {
        return Backlinks::collect(index, note).value_or(Backlinks::Result{});
    }));
}

void BacklinksPanel::fill(QTreeWidget* tree, const QList<Backlinks::Reference>& references)
{
    tree->clear();
    QTreeWidgetItem* group = nullptr;
    for (const Backlinks::Reference& ref : references) {
        if (!group || group->data(0, kPathRole).toString() != ref.sourcePath) {
            group = new QTreeWidgetItem(tree, {ref.sourceDisplay});
            group->setData(0, kPathRole, ref.sourcePath);
            group->setData(0, kLineRole, ref.line);
            group->setToolTip(0, ref.sourceDisplay);
            group->setExpanded(true);
        }
        auto* item = new QTreeWidgetItem(group, {ref.embed ? QStringLiteral("↪ ") + ref.context : ref.context});
        item->setData(0, kPathRole, ref.sourcePath);
        item->setData(0, kLineRole, ref.line);
        item->setToolTip(0, QStringLiteral("%1:%2").arg(ref.sourceDisplay).arg(ref.line));
    }
}

void BacklinksPanel::present(const Backlinks::Result& result)
{
    fill(m_links, result.links);
    m_links->setVisible(!result.links.isEmpty());
    m_linksLabel->setText(m_notePath.isEmpty() ? tr("Open a note to see what links to it.")
                          : result.links.isEmpty() ? tr("No notes link here.")
                                                   : tr("%n link(s)", nullptr, int(result.links.size())));
    fill(m_mentions, result.mentions);
    m_mentionsHeader->setText(tr("Unlinked mentions (%1)").arg(result.mentions.size()));
    m_mentionsHeader->setVisible(!m_notePath.isEmpty());
    m_mentions->setVisible(m_mentionsHeader->isChecked() && !result.mentions.isEmpty());
    emit updated(int(result.links.size()), int(result.mentions.size()));
}

} // namespace coeus
