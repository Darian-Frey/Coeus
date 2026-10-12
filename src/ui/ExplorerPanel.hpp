#pragma once

#include <QWidget>

class QFileSystemModel;
class QModelIndex;
class QTreeView;

namespace coeus {

// File explorer for the left dock: a QTreeView over a QFileSystemModel
// filtered to Markdown and attachment types (F-002).
class ExplorerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExplorerPanel(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    QString rootPath() const;

    QFileSystemModel* model() const { return m_model; }
    QTreeView* view() const { return m_view; }

signals:
    void fileActivated(const QString& absolutePath);
    // Emitted once the root folder's listing has been loaded by the model.
    void rootLoaded();

private:
    void onActivated(const QModelIndex& index);

    QFileSystemModel* m_model = nullptr;
    QTreeView* m_view = nullptr;
};

} // namespace coeus
