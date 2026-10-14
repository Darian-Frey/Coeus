#include "mcp/GuiLink.hpp"

#include "index/ContentHash.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>

namespace coeus {

namespace {

constexpr int kConnectMs = 300;
constexpr int kReplyMs = 3000;

} // namespace

QString GuiLink::socketPath(const QString& vaultPath)
{
    QString folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (folder.isEmpty())
        folder = QDir::tempPath();
    const QString canonical = QFileInfo(vaultPath).canonicalFilePath().isEmpty() ? vaultPath : QFileInfo(vaultPath).canonicalFilePath();
    const QByteArray hash = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return folder + QStringLiteral("/coeus-") + QString::fromLatin1(hash) + QStringLiteral(".sock");
}

GuiLink::GuiLink(const QString& vaultPath)
    : m_socket(socketPath(vaultPath))
{
}

std::optional<QJsonObject> GuiLink::ask(const QJsonObject& request) const
{
    if (!QFileInfo::exists(m_socket))
        return std::nullopt; // no window on this vault
    QLocalSocket socket;
    socket.connectToServer(m_socket);
    if (!socket.waitForConnected(kConnectMs))
        return std::nullopt; // a stale socket file: the window is gone
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(kReplyMs))
        return std::nullopt;
    QByteArray reply;
    while (!reply.contains('\n')) {
        if (!socket.waitForReadyRead(kReplyMs))
            return std::nullopt;
        reply += socket.readAll();
    }
    socket.disconnectFromServer();
    const QJsonDocument document = QJsonDocument::fromJson(reply.left(reply.indexOf('\n')));
    return document.isObject() ? std::optional(document.object()) : std::nullopt;
}

std::optional<GuiLink::Buffer> GuiLink::buffer(const QString& absolutePath) const
{
    const auto reply = ask({{QStringLiteral("op"), QStringLiteral("buffer")}, {QStringLiteral("path"), absolutePath}});
    if (!reply)
        return std::nullopt;
    return Buffer{reply->value(QStringLiteral("open")).toBool(), reply->value(QStringLiteral("modified")).toBool(),
                  reply->value(QStringLiteral("text")).toString()};
}

void GuiLink::wrote(const QString& absolutePath, const std::optional<QByteArray>& before, const QByteArray& after,
                    const QString& summary) const
{
    ask({{QStringLiteral("op"), QStringLiteral("wrote")},
         {QStringLiteral("path"), absolutePath},
         {QStringLiteral("before"), before ? QJsonValue(QString::fromLatin1(before->toBase64())) : QJsonValue()},
         {QStringLiteral("after"), QString::fromLatin1(ContentHash::of(after).toHex())},
         {QStringLiteral("summary"), summary}});
}

} // namespace coeus
