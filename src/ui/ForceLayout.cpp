#include "ui/ForceLayout.hpp"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coeus {

struct ForceLayout::Cell {
    double x0 = 0, y0 = 0, size = 0; // a square
    double mass = 0, cx = 0, cy = 0; // bodies inside and their centre
    int body = -1;                   // the one body of a leaf
    int child[4] = {-1, -1, -1, -1};
    bool leaf() const { return child[0] < 0 && child[1] < 0 && child[2] < 0 && child[3] < 0; }
};

namespace {

constexpr int kMaxDepth = 24; // bodies closer than this share a cell

// A small, repeatable nudge for bodies on top of one another.
double jiggle(int a, int b)
{
    return (double((a * 7919 + b * 104729) % 1000) / 1000.0 - 0.5) * 1e-3;
}

} // namespace

void ForceLayout::setGraph(const QList<QString>& keys, const QList<std::pair<int, int>>& edges)
{
    QHash<QString, Body> previous;
    for (int i = 0; i < m_keys.size(); ++i)
        previous.insert(m_keys[i], m_nodes[i]);
    m_keys = keys;
    m_nodes.assign(keys.size(), Body{});
    m_edges.assign(edges.begin(), edges.end());
    for (const auto& [a, b] : m_edges) {
        ++m_nodes[a].degree;
        ++m_nodes[b].degree;
    }

    std::vector<bool> placed(keys.size(), false);
    int kept = 0;
    for (int i = 0; i < keys.size(); ++i) {
        if (const auto it = previous.constFind(keys[i]); it != previous.constEnd()) {
            m_nodes[i].x = it->x;
            m_nodes[i].y = it->y;
            placed[i] = true;
            ++kept;
        }
    }
    // New nodes: beside a placed neighbour if they have one, else on a spiral (d3's phyllotaxis).
    const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < keys.size(); ++i) {
        if (placed[i])
            continue;
        const double angle = i * golden;
        bool beside = false;
        for (const auto& [a, b] : m_edges) {
            const int other = a == i ? b : b == i ? a : -1;
            if (other >= 0 && placed[other]) {
                m_nodes[i].x = m_nodes[other].x + 10.0 * std::cos(angle);
                m_nodes[i].y = m_nodes[other].y + 10.0 * std::sin(angle);
                beside = true;
                break;
            }
        }
        if (!beside) {
            const double radius = 10.0 * std::sqrt(0.5 + i);
            m_nodes[i].x = radius * std::cos(angle);
            m_nodes[i].y = radius * std::sin(angle);
        }
        placed[i] = true;
    }
    // A graph mostly laid out before needs a little warmth, a new one all of it.
    m_alpha = kept == 0 ? 1.0 : std::max(m_alpha, kept == keys.size() ? 0.1 : 0.5);
}

void ForceLayout::reheat(double alpha)
{
    m_alpha = std::max(m_alpha, alpha);
}

void ForceLayout::fix(int node, const QPointF& at)
{
    m_nodes[node].fixed = true;
    m_nodes[node].x = at.x();
    m_nodes[node].y = at.y();
    m_nodes[node].vx = m_nodes[node].vy = 0;
}

void ForceLayout::release(int node)
{
    m_nodes[node].fixed = false;
}

QRectF ForceLayout::bounds() const
{
    if (m_nodes.empty())
        return {};
    double left = m_nodes[0].x, right = left, top = m_nodes[0].y, bottom = top;
    for (const Body& body : m_nodes) {
        left = std::min(left, body.x);
        right = std::max(right, body.x);
        top = std::min(top, body.y);
        bottom = std::max(bottom, body.y);
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

void ForceLayout::repel()
{
    const QRectF box = bounds();
    std::vector<Cell> cells;
    cells.reserve(m_nodes.size() * 2 + 1);
    Cell root;
    root.x0 = box.left();
    root.y0 = box.top();
    root.size = std::max({box.width(), box.height(), 1.0});
    cells.push_back(root);

    // Build: each body goes down to a leaf of its own, splitting a leaf it lands in.
    for (int i = 0; i < int(m_nodes.size()); ++i) {
        int at = 0;
        for (int depth = 0;; ++depth) {
            Cell& cell = cells[at];
            if (cell.leaf() && cell.body < 0 && cell.mass == 0) {
                cell.body = i;
                cell.mass = 1;
                cell.cx = m_nodes[i].x;
                cell.cy = m_nodes[i].y;
                break;
            }
            // Fold the body into this cell's mass on the way down.
            const double mass = cell.mass + 1;
            cell.cx = (cell.cx * cell.mass + m_nodes[i].x) / mass;
            cell.cy = (cell.cy * cell.mass + m_nodes[i].y) / mass;
            cell.mass = mass;
            if (depth >= kMaxDepth)
                break; // as good as coincident: counted here
            const int resident = cell.leaf() ? cell.body : -1;
            cells[at].body = -1;
            const auto quadrant = [&cells, at](double x, double y) {
                const Cell& c = cells[at];
                const double half = c.size / 2;
                return (x >= c.x0 + half ? 1 : 0) + (y >= c.y0 + half ? 2 : 0);
            };
            const auto childOf = [&cells, at](int q) {
                if (cells[at].child[q] < 0) {
                    Cell child;
                    child.size = cells[at].size / 2;
                    child.x0 = cells[at].x0 + (q & 1 ? child.size : 0);
                    child.y0 = cells[at].y0 + (q & 2 ? child.size : 0);
                    cells.push_back(child);
                    cells[at].child[q] = int(cells.size() - 1);
                }
                return cells[at].child[q];
            };
            if (resident >= 0) {
                // The leaf's body moves down a level, keeping its own mass there.
                const int down = childOf(quadrant(m_nodes[resident].x, m_nodes[resident].y));
                cells[down].body = resident;
                cells[down].mass = 1;
                cells[down].cx = m_nodes[resident].x;
                cells[down].cy = m_nodes[resident].y;
            }
            at = childOf(quadrant(m_nodes[i].x, m_nodes[i].y));
        }
    }

    // Apply: far cells act as one body at their centre of mass.
    std::vector<int> stack;
    for (int i = 0; i < int(m_nodes.size()); ++i) {
        Body& body = m_nodes[i];
        stack.assign(1, 0);
        while (!stack.empty()) {
            const Cell& cell = cells[stack.back()];
            stack.pop_back();
            if (cell.mass == 0 || cell.body == i)
                continue;
            double dx = cell.cx - body.x;
            double dy = cell.cy - body.y;
            if (dx == 0 && dy == 0) {
                dx = jiggle(i, cell.body);
                dy = jiggle(cell.body, i);
            }
            double distance2 = dx * dx + dy * dy;
            const bool far = cell.size * cell.size / distance2 < kTheta * kTheta;
            if (!far && !cell.leaf()) {
                for (int c : cell.child)
                    if (c >= 0)
                        stack.push_back(c);
                continue;
            }
            distance2 = std::max(distance2, 1.0);
            const double weight = kCharge * m_alpha * cell.mass / distance2;
            body.vx += dx * weight;
            body.vy += dy * weight;
        }
    }
}

bool ForceLayout::step()
{
    if (settled() || m_nodes.empty())
        return false;
    repel();
    // Links: each pulls its ends towards kLinkDistance, the busier end moving less.
    for (const auto& [s, t] : m_edges) {
        Body& source = m_nodes[s];
        Body& target = m_nodes[t];
        double x = target.x + target.vx - source.x - source.vx;
        double y = target.y + target.vy - source.y - source.vy;
        if (x == 0 && y == 0) {
            x = jiggle(s, t);
            y = jiggle(t, s);
        }
        const double length = std::sqrt(x * x + y * y);
        const double strength = 1.0 / std::min(source.degree, target.degree);
        const double pull = (length - kLinkDistance) / length * m_alpha * strength;
        x *= pull;
        y *= pull;
        const double bias = double(source.degree) / (source.degree + target.degree);
        target.vx -= x * bias;
        target.vy -= y * bias;
        source.vx += x * (1 - bias);
        source.vy += y * (1 - bias);
    }
    for (Body& body : m_nodes) {
        if (body.fixed) {
            body.vx = body.vy = 0;
            continue;
        }
        body.vx -= body.x * kGravity * m_alpha;
        body.vy -= body.y * kGravity * m_alpha;
        body.vx *= 1 - kVelocityDecay;
        body.vy *= 1 - kVelocityDecay;
        body.x += body.vx;
        body.y += body.vy;
    }
    m_alpha *= 1 - kAlphaDecay;
    return true;
}

} // namespace coeus
