#include "ui/BookmarksPanel.hpp"

#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace coeus {

BookmarksPanel::BookmarksPanel(QWidget* parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
    , m_empty(new QLabel(this))
{
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_empty->setText(tr("Nothing starred yet. Star a note with ☆ in its header (Ctrl+Alt+S), or a search with ☆ in the "
                        "Search pane."));
    m_empty->setWordWrap(true);
    m_empty->setMargin(8);
    m_empty->setForegroundRole(QPalette::PlaceholderText);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_empty);
    layout->addWidget(m_tree, 1);

    const auto activate = [this](QTreeWidgetItem* item) {
        if (item)
            emit activated(m_tree->indexOfTopLevelItem(item));
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, activate);
    connect(m_tree, &QTreeWidget::itemActivated, this, activate);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &BookmarksPanel::showMenu);
    auto* remove = new QShortcut(QKeySequence::Delete, m_tree, nullptr, nullptr, Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, [this] {
        if (QTreeWidgetItem* item = m_tree->currentItem())
            emit removeRequested(m_tree->indexOfTopLevelItem(item));
    });
    setRows({});
}

void BookmarksPanel::setRows(const QList<Row>& rows)
{
    const int current = m_tree->currentItem() ? m_tree->indexOfTopLevelItem(m_tree->currentItem()) : -1;
    m_tree->clear();
    for (const Row& row : rows) {
        auto* item = new QTreeWidgetItem(m_tree, {(row.search ? QStringLiteral("⌕ ") : QStringLiteral("★ ")) + row.label, row.detail});
        item->setToolTip(0, row.missing ? tr("%1 (no longer there)").arg(row.detail) : row.detail);
        item->setForeground(1, palette().brush(QPalette::PlaceholderText));
        if (row.missing) {
            item->setForeground(0, palette().brush(QPalette::PlaceholderText));
            QFont font;
            font.setStrikeOut(true);
            item->setFont(0, font);
        }
    }
    if (current >= 0 && current < m_tree->topLevelItemCount())
        m_tree->setCurrentItem(m_tree->topLevelItem(current));
    m_empty->setVisible(rows.isEmpty());
    m_tree->setVisible(!rows.isEmpty());
}

void BookmarksPanel::showMenu(const QPoint& at)
{
    QTreeWidgetItem* item = m_tree->itemAt(at);
    if (!item)
        return;
    const int index = m_tree->indexOfTopLevelItem(item);
    const int count = m_tree->topLevelItemCount();
    QMenu menu(this);
    QAction* open = menu.addAction(tr("Open"));
    QAction* up = menu.addAction(tr("Move Up"));
    QAction* down = menu.addAction(tr("Move Down"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Remove Bookmark"));
    up->setEnabled(index > 0);
    down->setEnabled(index < count - 1);
    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(at));
    if (chosen == open)
        emit activated(index);
    else if (chosen == up)
        emit moveRequested(index, index - 1);
    else if (chosen == down)
        emit moveRequested(index, index + 1);
    else if (chosen == remove)
        emit removeRequested(index);
}

} // namespace coeus
