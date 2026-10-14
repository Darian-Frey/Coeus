#include "ui/RelatedPanel.hpp"

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

} // namespace

RelatedPanel::RelatedPanel(QWidget* parent)
    : QWidget(parent)
    , m_status(new QLabel(this))
    , m_tree(new QTreeWidget(this))
{
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_status->setWordWrap(true);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setWordWrap(false);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setMinimumHeight(60);
    auto open = [this](QTreeWidgetItem* item, int column) {
        if (column == 0)
            emit openRequested(item->data(0, kPathRole).toString(), item->data(0, kLineRole).toInt());
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, open);
    connect(m_tree, &QTreeWidget::itemActivated, this, open);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_status);
    layout->addWidget(m_tree);
    showNote(QString());
}

void RelatedPanel::setIndexPath(const QString& indexPath)
{
    m_indexPath = indexPath;
    refresh();
}

QString RelatedPanel::status() const
{
    return m_status->text();
}

QToolButton* RelatedPanel::linkButton(int row) const
{
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    return item ? qobject_cast<QToolButton*>(m_tree->itemWidget(item, 1)) : nullptr;
}

void RelatedPanel::showNote(const QString& notePath)
{
    m_notePath = notePath;
    refresh();
}

void RelatedPanel::refresh()
{
    const int generation = ++m_generation;
    if (m_notePath.isEmpty() || m_indexPath.isEmpty()) {
        m_tree->clear();
        m_tree->hide();
        m_status->setText(tr("Open a note to see the notes nearest it in meaning."));
        emit updated(0);
        return;
    }
    using Result = std::expected<SemanticSearch::Related, QString>;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_generation)
            present(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([index = m_indexPath, note = m_notePath] { return SemanticSearch::related(index, note); }));
}

void RelatedPanel::present(const std::expected<SemanticSearch::Related, QString>& result)
{
    m_tree->clear();
    if (!result) {
        m_status->setText(result.error());
        m_tree->hide();
        emit updated(0);
        return;
    }
    if (result->ownEmbedded == 0) {
        m_status->setText(result->ownPassages == 0 ? tr("This note has no text to compare yet.")
                                                   : tr("Waiting for this note to be embedded…"));
        m_tree->hide();
        emit updated(0);
        return;
    }
    m_status->setText(result->notes.isEmpty() ? tr("Nothing related that is not linked already.")
                      : result->ownEmbedded < result->ownPassages
                          ? tr("From %1 of this note's %2 passages; more to come").arg(result->ownEmbedded).arg(result->ownPassages)
                          : QString());
    m_status->setVisible(!m_status->text().isEmpty());
    for (const SemanticSearch::Hit& hit : result->notes) {
        auto* item = new QTreeWidgetItem(m_tree, {hit.heading.isEmpty() ? hit.display : hit.display + QStringLiteral(" › ") + hit.heading});
        item->setData(0, kPathRole, hit.notePath);
        item->setData(0, kLineRole, std::max(1, hit.startLine));
        item->setToolTip(0, QStringLiteral("%1\n\n%2\n\nRelatedness %3").arg(hit.display, hit.snippet).arg(double(hit.score), 0, 'f', 2));
        item->setForeground(0, palette().brush(QPalette::Link));
        auto* link = new QToolButton;
        link->setText(tr("Link here"));
        link->setAutoRaise(true);
        link->setToolTip(tr("Insert a link to %1 at the cursor").arg(hit.display));
        connect(link, &QToolButton::clicked, this, [this, path = hit.notePath] { emit linkRequested(path); });
        m_tree->setItemWidget(item, 1, link);
    }
    m_tree->setVisible(!result->notes.isEmpty());
    emit updated(int(result->notes.size()));
}

} // namespace coeus
