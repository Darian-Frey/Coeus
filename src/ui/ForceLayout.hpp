#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

namespace coeus {

// The graph view's force-directed layout (F-026), after d3-force: nodes
// repel one another (Barnes–Hut, so a step costs n log n), edges pull
// their ends to a resting length, a weak pull holds every node near the
// centre, and velocities decay. `alpha` cools from 1 each step until the
// layout settles; dragging or a changed graph warms it again. Pure and
// deterministic: the same graph lays out the same way.
class ForceLayout {
public:
    static constexpr double kLinkDistance = 40.0;
    static constexpr double kCharge = -90.0;     // repulsion; negative pushes apart
    static constexpr double kGravity = 0.04;     // towards the origin
    static constexpr double kVelocityDecay = 0.4;
    static constexpr double kAlphaDecay = 0.0228; // about 300 steps from 1 to settled
    static constexpr double kAlphaMin = 0.001;
    static constexpr double kTheta = 0.9;         // Barnes–Hut accuracy

    // Replaces the graph. Nodes whose key was laid out before keep their
    // place; a new node starts beside a placed neighbour, or on a spiral.
    void setGraph(const QList<QString>& keys, const QList<std::pair<int, int>>& edges);

    // One step; false once settled (nothing moves until reheat()).
    bool step();
    void reheat(double alpha = 0.3);
    bool settled() const { return m_alpha < kAlphaMin; }
    double alpha() const { return m_alpha; }

    int count() const { return int(m_nodes.size()); }
    QPointF position(int node) const { return {m_nodes[node].x, m_nodes[node].y}; }
    // A fixed node stays where it is put (while dragged); others flow around it.
    void fix(int node, const QPointF& at);
    void release(int node);
    QRectF bounds() const;

private:
    struct Body {
        double x = 0, y = 0, vx = 0, vy = 0;
        bool fixed = false;
        int degree = 0;
    };
    struct Cell; // a Barnes–Hut quadtree cell

    void repel();

    std::vector<Body> m_nodes;
    std::vector<std::pair<int, int>> m_edges;
    QList<QString> m_keys;
    double m_alpha = 1.0;
};

} // namespace coeus
