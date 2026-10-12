#include "ui/ExplorerPanel.hpp"

#include "vault/Vault.hpp"

#include <QFileSystemModel>
#include <QHeaderView>
#include <QTreeView>
#include <QVBoxLayout>

namespace coeus {

ExplorerPanel::ExplorerPanel(QWidget* parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_view(new QTreeView(this))
{
    m_model->setReadOnly(true);
    // No QDir::Hidden: .coeus/, .coeus-shared/ and .git/ stay out of the tree.
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setNameFilters(Vault::nameFilters());
    m_model->setNameFilterDisables(false);

    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    for (int column = 1; column < m_model->columnCount(); ++column)
        m_view->hideColumn(column);
    m_view->setUniformRowHeights(true);
    m_view->setAnimated(false);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    connect(m_view, &QTreeView::activated, this, &ExplorerPanel::onActivated);
    connect(m_view, &QTreeView::clicked, this, &ExplorerPanel::onActivated);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString& path) {
        if (path == m_model->rootPath())
            emit rootLoaded();
    });
}

void ExplorerPanel::setRootPath(const QString& path)
{
    m_view->setRootIndex(m_model->setRootPath(path));
}

QString ExplorerPanel::rootPath() const
{
    return m_model->rootPath();
}

void ExplorerPanel::onActivated(const QModelIndex& index)
{
    if (!index.isValid() || m_model->isDir(index))
        return;
    emit fileActivated(m_model->filePath(index));
}

} // namespace coeus
