#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <expected>

namespace coeus {

// The link graph (F-026): notes joined by their resolved links, and
// optionally tags as nodes joined to their notes and unresolved links as
// ghost notes. Loaded from the index off the UI thread; the local graph is
// a neighbourhood of it, which is pure.
class LinkGraph {
public:
    struct Node {
        enum class Kind { Note, Tag, Ghost };
        Kind kind = Kind::Note;
        QString key;     // "n:<root>/<path>", "t:<tag>", "g:<target, lower case>"; stable across loads
        QString label;   // a note's title, "#tag", or the link's target
        QString path;    // absolute, for a note
        QString project; // a note's project ("<root>" or "<root>/<folder>"), "" outside any
        QString tag;     // a note's first tag's top level ("area" for #area/sub), "" without one
        int degree = 0;  // edges in this graph
    };
    struct Edge {
        int a = 0;
        int b = 0;
    };
    struct Options {
        bool tags = true;         // tags as nodes, joined to their notes
        bool orphans = false;     // notes with no edge
        bool unresolved = false;  // links to notes that do not exist, as ghosts
        bool linkedRoots = false; // notes in linked roots, not only the vault
    };

    static std::expected<LinkGraph, QString> load(const QString& indexPath, const Options& options);

    // The nodes within `depth` edges of the note at `notePath` (absolute),
    // and the edges among them; empty if the note is not in the graph.
    LinkGraph neighbourhood(const QString& notePath, int depth) const;

    const QList<Node>& nodes() const { return m_nodes; }
    const QList<Edge>& edges() const { return m_edges; }
    int indexOf(const QString& key) const { return m_index.value(key, -1); }
    int indexOfPath(const QString& absolutePath) const;
    int noteCount() const;

    // Building, for load() and tests: a node once per key, an edge once per pair, none to itself.
    int addNode(const Node& node);
    void addEdge(int a, int b);
    // Drops notes without edges (never tags or ghosts, which exist only by their edges).
    void dropOrphans();

private:
    QList<Node> m_nodes;
    QList<Edge> m_edges;
    QHash<QString, int> m_index;
    QHash<quint64, bool> m_pairs;
};

} // namespace coeus
