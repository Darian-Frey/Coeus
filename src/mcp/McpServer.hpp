#pragma once

#include "index/IndexRoots.hpp"
#include "index/IndexStore.hpp"
#include "index/Indexer.hpp"
#include "mcp/GuiLink.hpp"
#include "search/Embedder.hpp"
#include "vault/GitIgnore.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <iosfwd>
#include <memory>
#include <optional>

namespace coeus {

// `coeus --mcp` (F-033, D-012): the Model Context Protocol over stdio, one
// JSON-RPC 2.0 message per line, answering initialize, ping, tools/list and
// tools/call with the tools of SPEC.md §MCP surface. Only protocol messages
// go to stdout; diagnostics go to stderr. Before each tool call it brings
// the vault's index up to date (an incremental scan of every present root,
// as the GUI's does on open), so it answers correctly whether or not Coeus
// is running, and alongside it: SQLite serialises their writes.
class McpServer {
public:
    // Protocol versions understood, newest first.
    static const QStringList& protocolVersions();
    static constexpr int kRescanIntervalMs = 2000; // at most one catch-up scan per this many ms

    explicit McpServer(const QString& vaultPath);
    ~McpServer();

    // Handles one message; the response line, or nullopt for a notification.
    std::optional<QByteArray> handle(const QByteArray& line);
    // Reads messages from `in` until it closes, writing responses to `out`.
    int run(std::istream& in, std::ostream& out);

    // For tests: scan before every call rather than at most every 2 s.
    void setAlwaysRescan(bool always) { m_alwaysRescan = always; }

private:
    QJsonObject initialize(const QJsonObject& params);
    QJsonObject callTool(const QJsonObject& params, QJsonObject* error);
    // Opens or refreshes the index; empty on success, else why it failed.
    QString refreshIndex();

    QString m_vaultPath;
    QString m_indexPath;
    std::optional<IndexStore> m_store;
    std::unique_ptr<Indexer> m_indexer;
    QList<IndexRoots::Root> m_roots;
    QHash<int, std::shared_ptr<GitIgnore>> m_ignores;
    QElapsedTimer m_sinceScan;
    GuiLink m_gui;
    std::unique_ptr<Embedder> m_embedder; // loaded on the first search by meaning
    QString m_embedderProblem;
    bool m_alwaysRescan = false;
};

} // namespace coeus
