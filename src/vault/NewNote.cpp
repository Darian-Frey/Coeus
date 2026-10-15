#include "vault/NewNote.hpp"

#include <QDir>
#include <QFileInfo>

namespace coeus {

std::expected<QString, QString> NewNote::pathFor(const QString& rootPath, const QString& folder, const QString& name)
{
    QString file = name.trimmed();
    if (file.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
        file.chop(3);
    if (file.trimmed().isEmpty())
        return std::unexpected(QStringLiteral("Give the note a name"));
    if (file.contains(QLatin1Char('/')) || file.contains(QLatin1Char('\\')))
        return std::unexpected(QStringLiteral("A name cannot contain a slash; put the folder in Folder"));
    if (file.startsWith(QLatin1Char('.')))
        return std::unexpected(QStringLiteral("A name starting with a dot would be hidden from Coeus"));
    const QString relative = QDir::cleanPath(folder.trimmed().isEmpty() ? QStringLiteral(".") : folder.trimmed());
    if (QDir::isAbsolutePath(relative) || relative == QLatin1String("..") || relative.startsWith(QLatin1String("../")))
        return std::unexpected(QStringLiteral("The folder must be inside %1").arg(QDir::toNativeSeparators(rootPath)));
    for (const QString& part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        if (part.startsWith(QLatin1Char('.')) && part != QLatin1String("."))
            return std::unexpected(QStringLiteral("Notes in hidden folders (%1) are not indexed").arg(part));
    const QString path = QDir::cleanPath(rootPath + QLatin1Char('/') + relative + QLatin1Char('/') + file + QStringLiteral(".md"));
    if (!path.startsWith(rootPath + QLatin1Char('/')))
        return std::unexpected(QStringLiteral("The folder must be inside %1").arg(QDir::toNativeSeparators(rootPath)));
    if (QFileInfo::exists(path))
        return std::unexpected(QStringLiteral("%1 already exists").arg(QDir(rootPath).relativeFilePath(path)));
    return path;
}

QString NewNote::blank(const QString& absolutePath)
{
    return QStringLiteral("# %1\n").arg(QFileInfo(absolutePath).completeBaseName());
}

} // namespace coeus
