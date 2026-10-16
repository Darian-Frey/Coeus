#pragma once

#include "index/LinkGraph.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace coeus {

class GraphView;

// The graph view (F-026) with its controls. Global: the vault's whole link
// graph, a workspace tab. Local: the current note's neighbourhood, a
// sidebar section. Loaded from the index on a worker thread; a newer load
// supersedes an older one still running. The choices are kept per user.
class GraphPanel : public QWidget {
    Q_OBJECT

public:
    enum class Scope { Global, Local };

    explicit GraphPanel(Scope scope, QWidget* parent = nullptr);

    void setIndexPath(const QString& indexPath);
    // Local: the note at the centre. Global: the note ringed as current.
    void showNote(const QString& absolutePath);
    void refresh();

    Scope scope() const { return m_scope; }
    GraphView* view() const { return m_view; }
    QString status() const;
    LinkGraph::Options options() const;
    QComboBox* colouring() const { return m_colouring; }
    QCheckBox* tagsBox() const { return m_tags; }
    QCheckBox* orphansBox() const { return m_orphans; }
    QCheckBox* unresolvedBox() const { return m_unresolved; }
    QCheckBox* linkedRootsBox() const { return m_linkedRoots; }
    QSpinBox* depthBox() const { return m_depth; }

    static constexpr int kMaxDepth = 3;

signals:
    void openRequested(const QString& absolutePath);
    void tagRequested(const QString& tag);
    // A load is on screen: its nodes and edges.
    void updated(int nodes, int edges);

private:
    void present(const std::expected<LinkGraph, QString>& graph);
    void saveChoices() const;
    QString settingsKey(const char* name) const;

    Scope m_scope;
    QString m_indexPath;
    QString m_notePath;
    int m_generation = 0;
    GraphView* m_view = nullptr;
    QLabel* m_status = nullptr;
    QComboBox* m_colouring = nullptr;
    QCheckBox* m_tags = nullptr;
    QCheckBox* m_orphans = nullptr;     // global only
    QCheckBox* m_unresolved = nullptr;  // global only
    QCheckBox* m_linkedRoots = nullptr; // global only
    QSpinBox* m_depth = nullptr;        // local only
    QToolButton* m_fit = nullptr;
};

} // namespace coeus
