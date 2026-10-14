#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QLocalServer;

namespace coeus {

// The window's side of the MCP link (F-033, AV-012; mcp/GuiLink is the
// server's): listens on the vault's socket while a vault is open, and
// answers `coeus --mcp` processes. It asks MainWindow, through callbacks,
// whether a note is open with unsaved changes, and tells it when one was
// written so it can offer an undo.
class McpLink : public QObject {
    Q_OBJECT

public:
    struct Buffer {
        bool open = false;
        bool modified = false;
        QString text;
    };
    using BufferFn = std::function<Buffer(const QString& absolutePath)>;
    using WroteFn = std::function<void(const QString& absolutePath, const std::optional<QByteArray>& before,
                                       const QByteArray& afterHash, const QString& summary)>;

    McpLink(BufferFn buffer, WroteFn wrote, QObject* parent = nullptr);
    ~McpLink() override;

    // Listens for `vaultPath` (stopping any previous vault); false if another
    // window already does.
    bool listen(const QString& vaultPath);
    void close();
    bool isListening() const;

private:
    void answer();

    BufferFn m_buffer;
    WroteFn m_wrote;
    QLocalServer* m_server = nullptr;
};

} // namespace coeus
