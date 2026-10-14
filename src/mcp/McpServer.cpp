#include "mcp/McpServer.hpp"

#include "index/IndexService.hpp"
#include "mcp/McpTools.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <iostream>
#include <string>

namespace coeus {

namespace {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

QByteArray encode(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject response(const QJsonValue& id, const QJsonObject& result)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
}

QJsonObject failure(const QJsonValue& id, int code, const QString& message)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

} // namespace

const QStringList& McpServer::protocolVersions()
{
    static const QStringList kVersions{QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
    return kVersions;
}

McpServer::McpServer(const QString& vaultPath)
    : m_vaultPath(vaultPath)
    , m_indexPath(IndexService::indexPath(vaultPath))
{
}

McpServer::~McpServer() = default;

std::optional<QByteArray> McpServer::handle(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return encode(failure(QJsonValue(), kParseError, QStringLiteral("Not JSON: %1").arg(parseError.errorString())));
    if (!document.isObject())
        return encode(failure(QJsonValue(), kInvalidRequest, QStringLiteral("Batches are not supported; send one message per line")));
    const QJsonObject message = document.object();
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    const bool notification = !message.contains(QStringLiteral("id"));
    if (message.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0") || method.isEmpty()) {
        if (notification || message.contains(QStringLiteral("result")) || message.contains(QStringLiteral("error")))
            return std::nullopt; // a response or a malformed notification: nothing to answer
        return encode(failure(id, kInvalidRequest, QStringLiteral("Not a JSON-RPC 2.0 request")));
    }
    if (notification)
        return std::nullopt; // notifications/initialized, notifications/cancelled, …: nothing to do

    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (method == QLatin1String("initialize"))
        return encode(response(id, initialize(params)));
    if (method == QLatin1String("ping"))
        return encode(response(id, {}));
    if (method == QLatin1String("tools/list"))
        return encode(response(id, {{QStringLiteral("tools"), McpTools::definitions()}}));
    if (method == QLatin1String("tools/call")) {
        QJsonObject error;
        const QJsonObject result = callTool(params, &error);
        if (!error.isEmpty())
            return encode(failure(id, error.value(QStringLiteral("code")).toInt(), error.value(QStringLiteral("message")).toString()));
        return encode(response(id, result));
    }
    return encode(failure(id, kMethodNotFound, QStringLiteral("Unknown method %1").arg(method)));
}

QJsonObject McpServer::initialize(const QJsonObject& params)
{
    const QString asked = params.value(QStringLiteral("protocolVersion")).toString();
    const QString version = protocolVersions().contains(asked) ? asked : protocolVersions().front();
    return {{QStringLiteral("protocolVersion"), version},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}}}},
            {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("coeus")},
                                                       {QStringLiteral("title"), QStringLiteral("Coeus")},
                                                       {QStringLiteral("version"), QStringLiteral(COEUS_VERSION)}}},
            {QStringLiteral("instructions"),
             QStringLiteral("Coeus indexes a Markdown vault (%1) and the project repositories linked to it. Projects keep "
                            "registers to a documentation standard: FEATURES (F-), CLAIMS (C-), DECISIONS (D-), ATTACK_VECTORS "
                            "(AV-), BUGS (BUG-) and IMPROVEMENTS (IMP-). Use project_status to see the projects and their names, "
                            "list_registers for a project's entries, search to find notes, and get_note to read one.")
                 .arg(m_vaultPath)}};
}

QString McpServer::refreshIndex()
{
    if (m_store && !m_alwaysRescan && m_sinceScan.isValid() && m_sinceScan.elapsed() < kRescanIntervalMs)
        return {};
    if (!m_store) {
        if (!QDir().mkpath(m_vaultPath + QStringLiteral("/.coeus")))
            return QStringLiteral("Cannot create %1/.coeus").arg(m_vaultPath);
        auto store = IndexStore::open(m_indexPath);
        if (!store)
            return store.error();
        m_store.emplace(std::move(*store));
        m_indexer = std::make_unique<Indexer>(*m_store);
        const auto reconciled = IndexRoots::reconcile(*m_store, m_vaultPath);
        if (!reconciled)
            return reconciled.error();
        m_roots = reconciled->roots;
        for (const QString& problem : reconciled->problems)
            std::cerr << "coeus --mcp: " << problem.toStdString() << '\n';
    }
    // Catch up with every edit since the last call, as the GUI's watcher would.
    for (const IndexRoots::Root& root : std::as_const(m_roots)) {
        if (!root.present)
            continue; // AV-010: never scanned, so never emptied
        auto& ignore = m_ignores[root.info.id];
        ignore = std::make_shared<GitIgnore>(root.info.path); // .gitignore may have changed
        const auto scanned = m_indexer->scan(root.info, [ignore](const QString& path, bool isDir) {
            return ignore->isIgnored(path, isDir);
        });
        if (!scanned)
            return scanned.error();
    }
    m_sinceScan.start();
    return {};
}

QJsonObject McpServer::callTool(const QJsonObject& params, QJsonObject* error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QJsonValue arguments = params.value(QStringLiteral("arguments"));
    if (name.isEmpty() || (!arguments.isUndefined() && !arguments.isNull() && !arguments.isObject())) {
        *error = {{QStringLiteral("code"), kInvalidParams}, {QStringLiteral("message"), QStringLiteral("tools/call needs a name and object arguments")}};
        return {};
    }
    bool known = false;
    for (const QJsonValue& definition : McpTools::definitions())
        known |= definition.toObject().value(QStringLiteral("name")).toString() == name;
    if (!known) {
        *error = {{QStringLiteral("code"), kInvalidParams}, {QStringLiteral("message"), QStringLiteral("Unknown tool: %1").arg(name)}};
        return {};
    }
    if (const QString problem = refreshIndex(); !problem.isEmpty()) {
        *error = {{QStringLiteral("code"), kInternalError}, {QStringLiteral("message"), QStringLiteral("The index is not usable: %1").arg(problem)}};
        return {};
    }
    const McpTools::Context context{m_vaultPath, m_indexPath, m_roots};
    const auto result = McpTools::call(context, name, arguments.toObject());
    if (!result) {
        // A tool's own failure is a result the model can read and act on, not a protocol error.
        return {{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                   {QStringLiteral("text"), result.error()}}}},
                {QStringLiteral("isError"), true}};
    }
    return {{QStringLiteral("content"),
             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                    {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(*result).toJson(QJsonDocument::Compact))}}}},
            {QStringLiteral("structuredContent"), *result},
            {QStringLiteral("isError"), false}};
}

int McpServer::run(std::istream& in, std::ostream& out)
{
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r")
            continue;
        if (const auto reply = handle(QByteArray::fromStdString(line))) {
            out.write(reply->constData(), reply->size());
            out.put('\n');
            out.flush();
        }
    }
    return 0;
}

} // namespace coeus
