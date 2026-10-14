#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace coeus {

// The MCP server's line to a running Coeus window on the same vault
// (F-033, AV-012): a per-user local socket, one JSON request and reply per
// connection. Blocking, with short timeouts, for `coeus --mcp`'s single
// thread; when no window is listening every call answers at once that none
// is. The window's side is ui/McpLink.
class GuiLink {
public:
    struct Buffer {
        bool open = false;
        bool modified = false;
        QString text; // the unsaved text, when modified
    };

    // Where the window listens for `vaultPath`: in the user's runtime folder
    // ($XDG_RUNTIME_DIR, private to them), named from a hash of the path.
    static QString socketPath(const QString& vaultPath);

    explicit GuiLink(const QString& vaultPath);

    // The open editor for the note at `absolutePath`, or nullopt when no window answers.
    std::optional<Buffer> buffer(const QString& absolutePath) const;
    // Tells the window a file was written, so it can offer to undo it:
    // `before` is the previous content, or nullopt if the file was created.
    void wrote(const QString& absolutePath, const std::optional<QByteArray>& before, const QByteArray& after,
               const QString& summary) const;

    // One request; the reply, or nullopt if no window answered.
    std::optional<QJsonObject> ask(const QJsonObject& request) const;

private:
    QString m_socket;
};

} // namespace coeus
