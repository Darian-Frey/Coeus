#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QFileSystemModel;
class QModelIndex;
class QTreeView;
class QToolButton;
class QVBoxLayout;

namespace coeus {

// One top-level node of the explorer: the vault or a linked root (F-034).
struct ExplorerRoot {
    QString name;  // shown in the header; the folder name for the vault
    QString path;  // absolute
    bool present = true;
};

// File explorer for the left dock (F-002, F-034). The vault and each linked
// root is a collapsible section with its own QTreeView over a QFileSystemModel
// filtered to Markdown and attachment types. A root missing on this machine is
// a greyed header with no tree.
class ExplorerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExplorerPanel(QWidget* parent = nullptr);

    // The first root is the vault.
    void setRoots(const QList<ExplorerRoot>& roots);
    // Only the vault.
    void setRootPath(const QString& path);
    QString rootPath() const;

    int sectionCount() const { return int(m_sections.size()); }
    QToolButton* sectionHeader(int index) const { return m_sections.value(index).header; }
    // Model and view of a section; null for a missing root. Section 0 is the vault.
    QFileSystemModel* model(int section = 0) const { return m_sections.value(section).model; }
    QTreeView* view(int section = 0) const { return m_sections.value(section).view; }

signals:
    void fileActivated(const QString& absolutePath);
    // Emitted once the vault folder's listing has been loaded by its model.
    void rootLoaded();

private:
    struct Section {
        QWidget* widget = nullptr;
        QToolButton* header = nullptr;
        QFileSystemModel* model = nullptr;
        QTreeView* view = nullptr;
    };

    Section createSection(const ExplorerRoot& root, bool isVault);
    void onActivated(QTreeView* view, const QModelIndex& index);

    QVBoxLayout* m_layout = nullptr;
    QList<Section> m_sections;
};

} // namespace coeus
