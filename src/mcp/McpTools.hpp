#pragma once

#include "index/IndexRoots.hpp"
#include "mcp/GuiLink.hpp"
#include "search/Embedder.hpp"

#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <expected>

namespace coeus {

// The tools of `coeus --mcp` (F-033, SPEC.md §MCP surface). Each takes its
// JSON arguments and returns the structured result, or an error the caller
// reports as a tool error. The read tools read the index and the vault's
// files. The write tools refuse unless the vault allows them, and write
// only through NextId and AtomicWriter, as the window does (AV-012).
class McpTools {
public:
    struct Context {
        QString vaultPath;
        QString indexPath;
        QList<IndexRoots::Root> roots;
        bool allowWrites = false;      // [mcp] allow_writes in config.toml
        const GuiLink* gui = nullptr;  // the window on this vault, if one is running
        QString holder = QStringLiteral("mcp"); // who holds a next_id reservation
        // The embedding model, loaded on first use; null with the reason if it cannot be.
        std::function<Embedder*(QString* why)> embedder;
    };

    static bool isWriteTool(const QString& name);
    // The refusal a write tool gives while the vault does not allow writes.
    static QString writesDisabled(const QString& vaultPath);

    // name, description and input schema of every tool, for tools/list.
    static QJsonArray definitions();
    // Runs `name`; an unknown name is an error.
    static std::expected<QJsonObject, QString> call(const Context& context, const QString& name, const QJsonObject& arguments);

    static std::expected<QJsonObject, QString> search(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> getNote(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> related(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> listRegisters(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> projectStatus(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> listIdeas(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> nextId(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> appendEntry(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> writeNote(const Context& context, const QJsonObject& arguments);
};

} // namespace coeus
