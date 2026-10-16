#pragma once

#include "index/LinkGraph.hpp"
#include "ui/ForceLayout.hpp"

#include <QHash>
#include <QSet>
#include <QWidget>

class QTimer;

namespace coeus {

// Draws a LinkGraph as it lays itself out (F-026): notes as dots sized by
// their links and coloured by project or tag, tags as accent dots, ghost
// notes hollow. Drag the background to pan, the wheel to zoom, a node to
// move it; hovering lights a node's neighbours; a click opens a note or
// filters by a tag. Fits the graph to the view while it settles, until the
// author pans or zooms; a double-click on the background fits it again.
class GraphView : public QWidget {
    Q_OBJECT

public:
    enum class Colouring { Project, Tag };

    explicit GraphView(QWidget* parent = nullptr);

    // Keeps the places of nodes it already showed.
    void setGraph(const LinkGraph& graph);
    const LinkGraph& graph() const { return m_graph; }
    void setColouring(Colouring colouring);
    Colouring colouring() const { return m_colouring; }
    // The note ringed as current; "" for none.
    void setCurrentNote(const QString& absolutePath);
    void fit();
    // Runs the layout to rest at once (tests).
    void settle();
    bool settled() const { return m_layout.settled(); }

    // Groups in the legend, largest first, as coloured.
    QStringList legend() const { return m_groups; }
    QColor nodeColour(int node) const;
    QPointF screenPosition(int node) const;
    int nodeAt(const QPointF& screen) const;
    int hovered() const { return m_hover; }

    static constexpr int kLegendSize = 8;
    static constexpr double kLabelScale = 1.4; // every label shows from this zoom
    static constexpr int kLandmarks = 12;      // the busiest nodes, labelled when zoomed out

signals:
    void noteActivated(const QString& absolutePath);
    void tagActivated(const QString& tag);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void tick();
    void animate();
    void assignGroups();
    double radius(int node) const; // in layout units
    QPointF toWorld(const QPointF& screen) const;
    QString groupOf(int node) const;

    LinkGraph m_graph;
    ForceLayout m_layout;
    QList<QSet<int>> m_neighbours;
    Colouring m_colouring = Colouring::Project;
    QStringList m_groups;           // legend order
    QHash<QString, int> m_groupIndex;
    QString m_current;
    int m_currentNode = -1;
    QSet<int> m_landmarks; // the busiest nodes, labelled at any zoom
    double m_scale = 1.0;
    QPointF m_offset;
    bool m_autoFit = true;
    int m_hover = -1;
    int m_drag = -1;
    bool m_panning = false;
    bool m_moved = false;
    QPointF m_pressAt;
    QPointF m_lastAt;
    QTimer* m_timer = nullptr;
};

} // namespace coeus
