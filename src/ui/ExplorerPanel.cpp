#include "ui/ExplorerPanel.hpp"

#include "vault/Vault.hpp"

#include <QFileSystemModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace coeus {

ExplorerPanel::ExplorerPanel(QWidget* parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

void ExplorerPanel::setRootPath(const QString& path)
{
    setRoots({ExplorerRoot{QDir(path).dirName(), path, true}});
}

QString ExplorerPanel::rootPath() const
{
    return m_sections.isEmpty() || !m_sections.front().model ? QString() : m_sections.front().model->rootPath();
}

void ExplorerPanel::setRoots(const QList<ExplorerRoot>& roots)
{
    for (const Section& section : std::as_const(m_sections))
        section.widget->deleteLater();
    m_sections.clear();

    for (qsizetype i = 0; i < roots.size(); ++i) {
        const Section section = createSection(roots[i], i == 0);
        // Expanded trees share the height; collapsed ones keep only their header.
        m_layout->addWidget(section.widget, section.view ? 1 : 0);
        m_sections.append(section);
    }
}

ExplorerPanel::Section ExplorerPanel::createSection(const ExplorerRoot& root, bool isVault)
{
    Section section;
    section.widget = new QWidget(this);
    auto* layout = new QVBoxLayout(section.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    section.header = new QToolButton(section.widget);
    section.header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    section.header->setAutoRaise(true);
    section.header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    section.header->setToolTip(root.path);
    QFont font = section.header->font();
    font.setBold(true);
    section.header->setFont(font);
    layout->addWidget(section.header);

    if (!root.present) {
        // AV-010: shown, greyed, never emptied.
        section.header->setText(tr("%1 — missing on this machine").arg(root.name));
        section.header->setToolTip(tr("%1\nNot found on this machine. Its notes stay in the index.").arg(root.path));
        section.header->setArrowType(Qt::NoArrow);
        section.header->setEnabled(false);
        return section;
    }

    section.header->setText(root.name);
    section.header->setCheckable(true);
    section.header->setChecked(true);
    section.header->setArrowType(Qt::DownArrow);

    section.model = new QFileSystemModel(section.widget);
    section.model->setReadOnly(true);
    // No QDir::Hidden: .coeus/, .coeus-shared/ and .git/ stay out of the tree.
    section.model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    section.model->setNameFilters(Vault::nameFilters());
    section.model->setNameFilterDisables(false);

    section.view = new QTreeView(section.widget);
    section.view->setModel(section.model);
    section.view->setHeaderHidden(true);
    for (int column = 1; column < section.model->columnCount(); ++column)
        section.view->hideColumn(column);
    section.view->setUniformRowHeights(true);
    section.view->setAnimated(false);
    section.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    section.view->setFrameShape(QFrame::NoFrame);
    section.view->setRootIndex(section.model->setRootPath(root.path));
    layout->addWidget(section.view);

    QTreeView* view = section.view;
    QWidget* widget = section.widget;
    connect(section.header, &QToolButton::toggled, this, [this, view, widget, header = section.header](bool open) {
        view->setVisible(open);
        header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_layout->setStretchFactor(widget, open ? 1 : 0);
    });
    connect(view, &QTreeView::activated, this, [this, view](const QModelIndex& index) { onActivated(view, index); });
    connect(view, &QTreeView::clicked, this, [this, view](const QModelIndex& index) { onActivated(view, index); });
    if (isVault) {
        QFileSystemModel* model = section.model;
        connect(model, &QFileSystemModel::directoryLoaded, this, [this, model](const QString& path) {
            if (path == model->rootPath())
                emit rootLoaded();
        });
    }
    return section;
}

void ExplorerPanel::onActivated(QTreeView* view, const QModelIndex& index)
{
    // One selection across all sections.
    for (const Section& section : std::as_const(m_sections)) {
        if (section.view && section.view != view)
            section.view->clearSelection();
    }
    auto* model = static_cast<QFileSystemModel*>(view->model());
    if (!index.isValid() || model->isDir(index))
        return;
    emit fileActivated(model->filePath(index));
}

} // namespace coeus
