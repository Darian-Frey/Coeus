#include "vault/Vault.hpp"

#include "vault/AtomicWriter.hpp"

#include <QFile>
#include <QFileInfo>

namespace coeus {

Vault::Vault(QDir root)
    : m_root(std::move(root))
{
}

std::expected<Vault, QString> Vault::open(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return std::unexpected(QStringLiteral("Vault folder does not exist: %1").arg(path));
    if (!info.isDir())
        return std::unexpected(QStringLiteral("Not a folder: %1").arg(path));
    if (!info.isReadable())
        return std::unexpected(QStringLiteral("Vault folder is not readable: %1").arg(path));
    return Vault(QDir(info.canonicalFilePath()));
}

QString Vault::rootPath() const
{
    return m_root.absolutePath();
}

QString Vault::name() const
{
    return m_root.dirName();
}

bool Vault::contains(const QString& absolutePath) const
{
    const QString canonical = QFileInfo(absolutePath).canonicalFilePath();
    if (canonical.isEmpty())
        return false;
    const QString root = rootPath();
    return canonical == root || canonical.startsWith(root + QLatin1Char('/'));
}

QString Vault::relativePath(const QString& absolutePath) const
{
    return m_root.relativeFilePath(absolutePath);
}

std::expected<QByteArray, QString> Vault::readBytes(const QString& absolutePath) const
{
    if (!contains(absolutePath))
        return std::unexpected(QStringLiteral("Outside the vault: %1").arg(absolutePath));

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Cannot read %1: %2")
                                   .arg(relativePath(absolutePath), file.errorString()));
    return file.readAll();
}

std::expected<QString, QString> Vault::readNote(const QString& absolutePath) const
{
    const auto bytes = readBytes(absolutePath);
    if (!bytes)
        return std::unexpected(bytes.error());
    return QString::fromUtf8(*bytes);
}

std::expected<void, QString> Vault::writeNote(const QString& absolutePath, QByteArrayView content) const
{
    const QFileInfo info(absolutePath);
    const bool inside = info.exists() ? contains(absolutePath) : contains(info.absolutePath());
    if (!inside)
        return std::unexpected(QStringLiteral("Outside the vault: %1").arg(absolutePath));
    return AtomicWriter::write(absolutePath, content);
}

const QStringList& Vault::nameFilters()
{
    static const QStringList kFilters{
        QStringLiteral("*.md"),
        QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.svg"),
        QStringLiteral("*.pdf"), QStringLiteral("*.mp3"), QStringLiteral("*.mp4"),
    };
    return kFilters;
}

bool Vault::isNote(const QString& path)
{
    return path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
}

} // namespace coeus
