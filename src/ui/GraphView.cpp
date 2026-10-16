#include "ui/GraphView.hpp"

#include "ui/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace coeus {

namespace {

constexpr int kFrameMs = 16;
constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 8.0;
constexpr double kFitMargin = 24.0;   // pixels around a fitted graph
constexpr double kClickSlop = 4.0;    // pixels a press may move and still be a click
constexpr int kLabelChars = 32;
constexpr int kSmallGraph = 300;      // nodes; smaller graphs step twice a frame

using Kind = LinkGraph::Node::Kind;

} // namespace

GraphView::GraphView(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setMouseTracking(true);
    setMinimumSize(120, 120);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    m_timer->setInterval(kFrameMs);
    connect(m_timer, &QTimer::timeout, this, &GraphView::tick);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void GraphView::setGraph(const LinkGraph& graph)
{
    m_graph = graph;
    QList<QString> keys;
    QList<std::pair<int, int>> edges;
    m_neighbours = QList<QSet<int>>(graph.nodes().size());
    for (const LinkGraph::Node& node : graph.nodes())
        keys.append(node.key);
    for (const LinkGraph::Edge& edge : graph.edges()) {
        edges.append({edge.a, edge.b});
        m_neighbours[edge.a].insert(edge.b);
        m_neighbours[edge.b].insert(edge.a);
    }
    m_layout.setGraph(keys, edges);
    // A dozen of the busiest nodes keep their labels when zoomed out, so a
    // crowd of equally busy notes does not bury the view in words.
    QList<int> order;
    for (int i = 0; i < graph.nodes().size(); ++i)
        if (graph.nodes()[i].degree >= 3)
            order.append(i);
    std::stable_sort(order.begin(), order.end(),
                     [&graph](int a, int b) { return graph.nodes()[a].degree > graph.nodes()[b].degree; });
    m_landmarks = QSet<int>(order.begin(), order.begin() + std::min<qsizetype>(order.size(), kLandmarks));
    m_hover = -1;
    m_drag = -1;
    m_currentNode = m_current.isEmpty() ? -1 : m_graph.indexOfPath(m_current);
    assignGroups();
    animate();
    update();
}

void GraphView::setColouring(Colouring colouring)
{
    m_colouring = colouring;
    assignGroups();
    update();
}

void GraphView::setCurrentNote(const QString& absolutePath)
{
    m_current = absolutePath;
    m_currentNode = absolutePath.isEmpty() ? -1 : m_graph.indexOfPath(absolutePath);
    update();
}

QString GraphView::groupOf(int node) const
{
    const LinkGraph::Node& n = m_graph.nodes()[node];
    if (m_colouring == Colouring::Tag)
        return n.kind == Kind::Ghost ? QString() : n.tag;
    return n.kind == Kind::Note ? n.project : QString();
}

void GraphView::assignGroups()
{
    // Largest first, so the commonest groups take the first, most distinct colours.
    QHash<QString, int> sizes;
    for (int i = 0; i < m_graph.nodes().size(); ++i)
        if (const QString group = groupOf(i); !group.isEmpty())
            ++sizes[group];
    m_groups = sizes.keys();
    std::sort(m_groups.begin(), m_groups.end(), [&sizes](const QString& a, const QString& b) {
        return sizes[a] != sizes[b] ? sizes[a] > sizes[b] : a < b;
    });
    m_groupIndex.clear();
    for (int i = 0; i < m_groups.size(); ++i)
        m_groupIndex.insert(m_groups[i], i);
}

QColor GraphView::nodeColour(int node) const
{
    const LinkGraph::Node& n = m_graph.nodes()[node];
    if (n.kind == Kind::Ghost)
        return palette().color(QPalette::PlaceholderText);
    const QString group = groupOf(node);
    if (!group.isEmpty())
        return Theme::instance().category(m_groupIndex.value(group));
    if (n.kind == Kind::Tag)
        return Theme::instance().colour(Theme::Colour::Accent);
    return palette().color(QPalette::PlaceholderText); // a note outside any group
}

double GraphView::radius(int node) const
{
    return 3.5 + 1.6 * std::sqrt(double(m_graph.nodes()[node].degree));
}

QPointF GraphView::screenPosition(int node) const
{
    return m_layout.position(node) * m_scale + m_offset;
}

QPointF GraphView::toWorld(const QPointF& screen) const
{
    return (screen - m_offset) / m_scale;
}

int GraphView::nodeAt(const QPointF& screen) const
{
    // The nearest node whose dot (at least a few pixels wide) is under the point.
    int best = -1;
    double bestDistance = 0;
    for (int i = 0; i < m_layout.count(); ++i) {
        const double reach = std::max(radius(i) * m_scale, 5.0);
        const QPointF d = screenPosition(i) - screen;
        const double distance = std::hypot(d.x(), d.y());
        if (distance <= reach && (best < 0 || distance < bestDistance)) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void GraphView::fit()
{
    if (m_layout.count() == 0)
        return;
    const QRectF bounds = m_layout.bounds().adjusted(-10, -10, 10, 10);
    const double sx = (width() - 2 * kFitMargin) / std::max(bounds.width(), 1.0);
    const double sy = (height() - 2 * kFitMargin) / std::max(bounds.height(), 1.0);
    m_scale = std::clamp(std::min(sx, sy), kMinScale, 2.0);
    m_offset = QPointF(width() / 2.0, height() / 2.0) - bounds.center() * m_scale;
    update();
}

void GraphView::settle()
{
    while (m_layout.step()) {
    }
    if (m_autoFit)
        fit();
    update();
}

void GraphView::animate()
{
    if (!m_layout.settled() && isVisible() && !m_timer->isActive())
        m_timer->start();
}

void GraphView::tick()
{
    if (!isVisible()) {
        m_timer->stop(); // resumed on show
        return;
    }
    bool moving = m_layout.step();
    if (moving && m_layout.count() < kSmallGraph)
        m_layout.step();
    if (m_autoFit && m_drag < 0)
        fit();
    update();
    if (!moving && m_drag < 0)
        m_timer->stop();
}

void GraphView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_autoFit)
        fit();
    animate();
}

void GraphView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_autoFit)
        fit();
}

void GraphView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& colours = palette();
    if (m_graph.nodes().isEmpty())
        return;

    const int focus = m_drag >= 0 ? m_drag : m_hover;
    const auto lit = [this, focus](int node) { return focus < 0 || node == focus || m_neighbours[focus].contains(node); };

    // Edges: faint, and the focused node's in the accent.
    QColor edge = colours.color(QPalette::Text);
    edge.setAlphaF(focus >= 0 ? 0.08 : 0.22);
    QList<QLineF> plain;
    QList<QLineF> bright;
    for (const LinkGraph::Edge& e : m_graph.edges()) {
        const QLineF line(screenPosition(e.a), screenPosition(e.b));
        (focus >= 0 && (e.a == focus || e.b == focus) ? bright : plain).append(line);
    }
    painter.setPen(QPen(edge, 1.0));
    painter.drawLines(plain);
    if (!bright.isEmpty()) {
        painter.setPen(QPen(Theme::instance().colour(Theme::Colour::Accent), 1.5));
        painter.drawLines(bright);
    }

    // Nodes, then labels over them.
    const QColor ring = Theme::instance().colour(Theme::Colour::Accent);
    for (int i = 0; i < m_layout.count(); ++i) {
        const double r = std::max(radius(i) * m_scale, 2.0);
        QColor fill = nodeColour(i);
        if (!lit(i))
            fill.setAlphaF(0.25);
        const QPointF at = screenPosition(i);
        if (m_graph.nodes()[i].kind == Kind::Ghost) {
            painter.setPen(QPen(fill, 1.2));
            painter.setBrush(Qt::NoBrush);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
        }
        painter.drawEllipse(at, r, r);
        if (i == m_currentNode) {
            painter.setPen(QPen(ring, 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(at, r + 3, r + 3);
        }
    }
    const QFontMetrics metrics(font());
    for (int i = 0; i < m_layout.count(); ++i) {
        const LinkGraph::Node& node = m_graph.nodes()[i];
        const bool show = i == focus || i == m_currentNode || (focus >= 0 && lit(i)) || m_scale >= kLabelScale
                          || (m_landmarks.contains(i) && m_scale >= kLabelScale / 4);
        if (!show)
            continue;
        QColor text = colours.color(QPalette::Text);
        if (!lit(i))
            text.setAlphaF(0.3);
        painter.setPen(text);
        const QString label = metrics.elidedText(node.label, Qt::ElideRight, metrics.averageCharWidth() * kLabelChars);
        const QPointF at = screenPosition(i);
        const double below = std::max(radius(i) * m_scale, 2.0) + metrics.ascent() + 2;
        painter.drawText(QPointF(at.x() - metrics.horizontalAdvance(label) / 2.0, at.y() + below), label);
    }

    // Legend: the largest groups and their colours.
    painter.setPen(colours.color(QPalette::Text));
    int y = 8 + metrics.ascent();
    for (int i = 0; i < std::min<qsizetype>(m_groups.size(), kLegendSize); ++i) {
        painter.setBrush(Theme::instance().category(i));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(14, y - metrics.ascent() / 2.0 + 1), 4, 4);
        painter.setPen(colours.color(QPalette::Text));
        painter.drawText(QPointF(24, y), m_colouring == Colouring::Tag ? QLatin1Char('#') + m_groups[i] : m_groups[i]);
        y += metrics.height();
    }
    if (m_groups.size() > kLegendSize) {
        painter.setPen(colours.color(QPalette::PlaceholderText));
        painter.drawText(QPointF(24, y), tr("and %n more", nullptr, int(m_groups.size() - kLegendSize)));
    }
}

void GraphView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    m_pressAt = m_lastAt = event->position();
    m_moved = false;
    m_drag = nodeAt(event->position());
    m_panning = m_drag < 0;
    if (m_drag >= 0)
        m_layout.fix(m_drag, toWorld(event->position()));
    else
        setCursor(Qt::ClosedHandCursor);
}

void GraphView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    if (!m_moved && (at - m_pressAt).manhattanLength() > kClickSlop && (m_drag >= 0 || m_panning))
        m_moved = true;
    if (m_drag >= 0) {
        if (m_moved) {
            m_layout.fix(m_drag, toWorld(at));
            m_layout.reheat();
            m_autoFit = false; // the author is arranging: hold the view still
            animate();
        }
    } else if (m_panning) {
        m_offset += at - m_lastAt;
        m_autoFit = false;
        update();
    } else {
        const int over = nodeAt(at);
        if (over != m_hover) {
            m_hover = over;
            setCursor(over >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            setToolTip(over >= 0 ? m_graph.nodes()[over].label : QString());
            update();
        }
    }
    m_lastAt = at;
}

void GraphView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mouseReleaseEvent(event);
    const int node = m_drag;
    m_drag = -1;
    m_panning = false;
    unsetCursor();
    if (node < 0)
        return;
    m_layout.release(node);
    if (m_moved) {
        animate();
        return;
    }
    // A click: the note opens, a tag filters the explorer.
    const LinkGraph::Node& n = m_graph.nodes()[node];
    if (n.kind == Kind::Note)
        emit noteActivated(n.path);
    else if (n.kind == Kind::Tag)
        emit tagActivated(n.key.mid(2));
}

void GraphView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (nodeAt(event->position()) < 0) {
        m_autoFit = true;
        fit();
    }
}

void GraphView::wheelEvent(QWheelEvent* event)
{
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double scale = std::clamp(m_scale * factor, kMinScale, kMaxScale);
    // Zoom about the pointer: the point under it stays under it.
    const QPointF world = toWorld(event->position());
    m_scale = scale;
    m_offset = event->position() - world * m_scale;
    m_autoFit = false;
    update();
}

void GraphView::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (m_hover >= 0 && m_drag < 0) {
        m_hover = -1;
        update();
    }
}

} // namespace coeus
