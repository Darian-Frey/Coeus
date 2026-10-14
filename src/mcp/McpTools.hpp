#pragma once

#include "index/IndexRoots.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <expected>

namespace coeus {

// The read tools of `coeus --mcp` (F-033, SPEC.md §MCP surface). Each takes
// its JSON arguments and returns the structured result, or an error the
// caller reports as a tool error. They read the index (read-only
// connections) and the vault's files; none writes.
class McpTools {
public:
    struct Context {
        QString vaultPath;
        QString indexPath;
        QList<IndexRoots::Root> roots;
    };

    // name, description and input schema of every tool, for tools/list.
    static QJsonArray definitions();
    // Runs `name`; an unknown name is an error.
    static std::expected<QJsonObject, QString> call(const Context& context, const QString& name, const QJsonObject& arguments);

    static std::expected<QJsonObject, QString> search(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> getNote(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> listRegisters(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> projectStatus(const Context& context, const QJsonObject& arguments);
    static std::expected<QJsonObject, QString> listIdeas(const Context& context, const QJsonObject& arguments);
};

} // namespace coeus
