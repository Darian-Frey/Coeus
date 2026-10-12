#include "vault/TempSweeper.hpp"

#include "vault/AtomicWriter.hpp"
#include "vault/VaultLogging.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace coeus {

namespace {

void sweepDirectory(const QString& path, const QDateTime& cutoff, TempSweeper::Result& result)
{
    const QDir dir(path);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.isSymLink())
            continue; // never follow or remove links
        if (entry.isDir()) {
            if (entry.fileName() != QLatin1String(".git"))
                sweepDirectory(entry.filePath(), cutoff, result);
            continue;
        }
        if (!entry.isFile() || !TempSweeper::isTempName(entry.fileName()))
            continue;
        if (entry.lastModified() > cutoff)
            continue;
        if (QFile::remove(entry.filePath())) {
            ++result.removed;
            qCDebug(lcVault, "Removed stale temporary file %s", qPrintable(entry.filePath()));
        } else {
            result.failures.append(entry.filePath());
        }
    }
}

} // namespace

bool TempSweeper::isTempName(const QString& fileName)
{
    // "." + basename + ".coeus-tmp-" + lowercase hex (AtomicWriter::write).
    static const QRegularExpression kPattern(
        QStringLiteral("^\\..+%1[0-9a-f]+$")
            .arg(QRegularExpression::escape(QLatin1String(AtomicWriter::kTempMarker))));
    return kPattern.match(fileName).hasMatch();
}

TempSweeper::Result TempSweeper::sweep(const QString& root, const QDateTime& now, std::chrono::seconds minAge)
{
    Result result;
    if (QFileInfo(root).isDir())
        sweepDirectory(root, now.addSecs(-minAge.count()), result);
    if (result.removed > 0)
        qCInfo(lcVault, "Removed %d stale temporary file(s) under %s", result.removed, qPrintable(root));
    for (const QString& failure : std::as_const(result.failures))
        qCWarning(lcVault, "Could not remove stale temporary file %s", qPrintable(failure));
    return result;
}

} // namespace coeus
