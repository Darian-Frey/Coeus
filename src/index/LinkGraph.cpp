#include "index/LinkGraph.hpp"

#include "index/Database.hpp"

#include <QFileInfo>
#include <QSet>

namespace coeus {

namespace {

constexpr std::int64_t kVaultRootId = 0; // IndexRoots: the vault is root 0

} // namespace

int LinkGraph::addNode(const Node& node)
{
    if (const int at = indexOf(node.key); at >= 0)
        return at;
    m_nodes.append(node);
    m_nodes.back().degree = 0;
    m_index.insert(node.key, int(m_nodes.size() - 1));
    return int(m_nodes.size() - 1);
}

void LinkGraph::addEdge(int a, int b)
{
    if (a == b || a < 0 || b < 0)
        return;
    const quint64 pair = (quint64(std::min(a, b)) << 32) | quint64(std::max(a, b));
    if (m_pairs.contains(pair))
        return;
    m_pairs.insert(pair, true);
    m_edges.append({a, b});
    ++m_nodes[a].degree;
    ++m_nodes[b].degree;
}

void LinkGraph::dropOrphans()
{
    LinkGraph kept;
    QHash<int, int> moved;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].degree > 0 || m_nodes[i].kind != Node::Kind::Note)
            moved.insert(i, kept.addNode(m_nodes[i]));
    }
    for (const Edge& edge : std::as_const(m_edges))
        kept.addEdge(moved.value(edge.a, -1), moved.value(edge.b, -1));
    *this = std::move(kept);
}

int LinkGraph::indexOfPath(const QString& absolutePath) const
{
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].kind == Node::Kind::Note && m_nodes[i].path == absolutePath)
            return i;
    return -1;
}

int LinkGraph::noteCount() const
{
    int notes = 0;
    for (const Node& node : m_nodes)
        notes += node.kind == Node::Kind::Note ? 1 : 0;
    return notes;
}

LinkGraph LinkGraph::neighbourhood(const QString& notePath, int depth) const
{
    LinkGraph local;
    const int centre = indexOfPath(notePath);
    if (centre < 0)
        return local;
    QList<QList<int>> adjacent(m_nodes.size());
    for (const Edge& edge : m_edges) {
        adjacent[edge.a].append(edge.b);
        adjacent[edge.b].append(edge.a);
    }
    // Breadth first, so nodes are listed nearest first and the centre is node 0.
    QHash<int, int> distance{{centre, 0}};
    QList<int> order{centre};
    for (int head = 0; head < order.size(); ++head) {
        const int at = order[head];
        if (distance[at] >= depth)
            continue;
        for (int next : std::as_const(adjacent[at])) {
            if (!distance.contains(next)) {
                distance.insert(next, distance[at] + 1);
                order.append(next);
            }
        }
    }
    QHash<int, int> moved;
    for (int i : std::as_const(order))
        moved.insert(i, local.addNode(m_nodes[i]));
    for (const Edge& edge : m_edges)
        if (moved.contains(edge.a) && moved.contains(edge.b))
            local.addEdge(moved[edge.a], moved[edge.b]);
    return local;
}

std::expected<LinkGraph, QString> LinkGraph::load(const QString& indexPath, const Options& options)
{
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());

    LinkGraph graph;
    QHash<std::int64_t, int> noteNode; // notes.id → node
    {
        auto query = db->prepare("SELECT n.id, n.root_id, r.name, r.path, n.path, n.title, p.path, pr.name "
                                 "FROM notes n JOIN roots r ON r.id = n.root_id "
                                 "LEFT JOIN projects p ON p.id = n.project_id LEFT JOIN roots pr ON pr.id = p.root_id "
                                 "ORDER BY n.root_id, n.path");
        if (!query)
            return std::unexpected(query.error());
        while (true) {
            auto row = query->step();
            if (!row)
                return std::unexpected(row.error());
            if (!*row)
                break;
            if (!options.linkedRoots && query->int64(1) != kVaultRootId)
                continue;
            Node node;
            node.key = QStringLiteral("n:%1/%2").arg(query->text(2), query->text(4));
            node.label = query->text(5);
            node.path = query->text(3) + QLatin1Char('/') + query->text(4);
            if (!query->isNull(6)) {
                // A project is named by its folder, or by its root when it is the root.
                const QString folder = query->text(6);
                node.project = folder.isEmpty() ? query->text(7) : folder.section(QLatin1Char('/'), -1);
            }
            noteNode.insert(query->int64(0), graph.addNode(node));
        }
    }
    {
        auto query = db->prepare("SELECT src, target_note, target_raw FROM links ORDER BY src, line");
        if (!query)
            return std::unexpected(query.error());
        while (true) {
            auto row = query->step();
            if (!row)
                return std::unexpected(row.error());
            if (!*row)
                break;
            const int from = noteNode.value(query->int64(0), -1);
            if (from < 0)
                continue;
            if (!query->isNull(1)) {
                graph.addEdge(from, noteNode.value(query->int64(1), -1)); // -1 outside the graph's roots
                continue;
            }
            // Unresolved: a note not written yet, not a missing attachment.
            const QString target = query->text(2).trimmed();
            const QString suffix = QFileInfo(target).suffix().toLower();
            if (!options.unresolved || target.isEmpty() || (!suffix.isEmpty() && suffix != QLatin1String("md")))
                continue;
            Node ghost;
            ghost.kind = Node::Kind::Ghost;
            ghost.key = QStringLiteral("g:") + target.toLower();
            ghost.label = target;
            graph.addEdge(from, graph.addNode(ghost));
        }
    }
    {
        auto query = db->prepare("SELECT note, tag FROM tags ORDER BY note, coalesce(line, 0)");
        if (!query)
            return std::unexpected(query.error());
        while (true) {
            auto row = query->step();
            if (!row)
                return std::unexpected(row.error());
            if (!*row)
                break;
            const int note = noteNode.value(query->int64(0), -1);
            if (note < 0)
                continue;
            const QString tag = query->text(1);
            if (graph.m_nodes[note].tag.isEmpty())
                graph.m_nodes[note].tag = tag.section(QLatin1Char('/'), 0, 0);
            if (!options.tags)
                continue;
            Node node;
            node.kind = Node::Kind::Tag;
            node.key = QStringLiteral("t:") + tag;
            node.label = QLatin1Char('#') + tag;
            node.tag = tag.section(QLatin1Char('/'), 0, 0);
            graph.addEdge(note, graph.addNode(node));
        }
    }
    if (!options.orphans)
        graph.dropOrphans();
    return graph;
}

} // namespace coeus
