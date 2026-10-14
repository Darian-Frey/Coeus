#include "ui/McpLink.hpp"

#include "mcp/GuiLink.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

namespace coeus {

McpLink::McpLink(BufferFn buffer, WroteFn wrote, QObject* parent)
    : QObject(parent)
    , m_buffer(std::move(buffer))
    , m_wrote(std::move(wrote))
    , m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption); // this user's processes only
    connect(m_server, &QLocalServer::newConnection, this, &McpLink::answer);
}

McpLink::~McpLink()
{
    close();
}

bool McpLink::listen(const QString& vaultPath)
{
    close();
    const QString path = GuiLink::socketPath(vaultPath);
    // A socket left by a window that crashed answers no one: replace it.
    // One that answers belongs to another window on the same vault.
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(100)) {
        probe.disconnectFromServer();
        return false;
    }
    QLocalServer::removeServer(path);
    return m_server->listen(path);
}

void McpLink::close()
{
    if (m_server->isListening())
        m_server->close(); // removes the socket file
}

bool McpLink::isListening() const
{
    return m_server->isListening();
}

void McpLink::answer()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
            if (!socket->canReadLine())
                return;
            const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
            const QString op = request.value(QStringLiteral("op")).toString();
            const QString path = request.value(QStringLiteral("path")).toString();
            QJsonObject reply{{QStringLiteral("ok"), true}};
            if (op == QLatin1String("buffer")) {
                const Buffer buffer = m_buffer(path);
                reply.insert(QStringLiteral("open"), buffer.open);
                reply.insert(QStringLiteral("modified"), buffer.modified);
                if (buffer.modified)
                    reply.insert(QStringLiteral("text"), buffer.text);
            } else if (op == QLatin1String("wrote")) {
                const QJsonValue before = request.value(QStringLiteral("before"));
                m_wrote(path, before.isString() ? std::optional(QByteArray::fromBase64(before.toString().toLatin1())) : std::nullopt,
                        QByteArray::fromHex(request.value(QStringLiteral("after")).toString().toLatin1()),
                        request.value(QStringLiteral("summary")).toString());
            } else {
                reply = {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown op")}};
            }
            socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
        });
    }
}

} // namespace coeus
