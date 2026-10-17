#include "sync/SyncConflicts.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace coeus {

std::optional<QString> SyncConflicts::originalOf(const QString& copyName)
{
    static const QRegularExpression kCopy(QStringLiteral(R"(^(.+)\.conflict\d+$)"));
    const auto match = kCopy.match(copyName);
    if (!match.hasMatch())
        return std::nullopt;
    return match.captured(1);
}

QList<QFileInfo> SyncConflicts::syncedFiles(const QString& vaultPath, bool shared)
{
    QList<QFileInfo> files;
    QList<QString> folders{QDir(vaultPath).absolutePath()};
    for (qsizetype i = 0; i < folders.size(); ++i) {
        const QDir folder(folders[i]);
        for (const QFileInfo& entry : folder.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                                           QDir::Name)) {
            if (entry.isDir()) {
                const bool atRoot = i == 0;
                if (atRoot && (entry.fileName() == QLatin1String(".coeus") || entry.fileName() == QLatin1String(".git")
                               || (!shared && entry.fileName() == QLatin1String(".coeus-shared"))))
                    continue;
                folders.append(entry.absoluteFilePath());
            } else if (!entry.fileName().contains(QLatin1String(".coeus-tmp-"))) {
                files.append(entry);
            }
        }
    }
    return files;
}

QList<SyncConflict> SyncConflicts::find(const QString& vaultPath)
{
    QList<SyncConflict> conflicts;
    for (const QFileInfo& file : syncedFiles(vaultPath, false))
        if (const auto original = originalOf(file.fileName()))
            conflicts.append({file.absolutePath() + QLatin1Char('/') + *original, file.absoluteFilePath()});
    std::sort(conflicts.begin(), conflicts.end(),
              [](const SyncConflict& a, const SyncConflict& b) { return a.copyPath < b.copyPath; });
    return conflicts;
}

} // namespace coeus
