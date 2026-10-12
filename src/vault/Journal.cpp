#include "vault/Journal.hpp"

#include "vault/AtomicWriter.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace coeus {

namespace {

// Line 1 identifies the format; lines 2 and 3 carry the note and its base
// version; a blank line separates the header from the buffer, kept verbatim.
constexpr QByteArrayView kMagic("coeus-journal 1");
constexpr QByteArrayView kPathKey("path: ");
constexpr QByteArrayView kBaseKey("base: ");

} // namespace

QString Journal::directory(const QString& vaultPath)
{
    return vaultPath + QStringLiteral("/.coeus/journal");
}

QString Journal::fileFor(const QString& vaultPath, const QString& notePath)
{
    const QByteArray hash = QCryptographicHash::hash(notePath.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
    return directory(vaultPath) + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".md");
}

QByteArray Journal::serialise(const QString& notePath, const QByteArray& baseHash, const QString& text)
{
    QByteArray out;
    out.append(kMagic).append('\n');
    out.append(kPathKey).append(notePath.toUtf8()).append('\n');
    out.append(kBaseKey).append(baseHash.toHex()).append('\n');
    out.append('\n');
    out.append(text.toUtf8());
    return out;
}

std::expected<Journal::Entry, QString> Journal::parse(const QByteArray& content)
{
    QByteArrayView rest(content);
    auto takeLine = [&rest]() -> std::optional<QByteArrayView> {
        const qsizetype end = rest.indexOf('\n');
        if (end < 0)
            return std::nullopt;
        const QByteArrayView line = rest.first(end);
        rest = rest.sliced(end + 1);
        return line;
    };

    const auto magic = takeLine();
    const auto path = takeLine();
    const auto base = takeLine();
    const auto blank = takeLine();
    if (!magic || *magic != kMagic || !path || !path->startsWith(kPathKey) || !base || !base->startsWith(kBaseKey)
        || !blank || !blank->isEmpty())
        return std::unexpected(QStringLiteral("Not a Coeus journal entry"));

    Entry entry;
    entry.notePath = QString::fromUtf8(path->sliced(kPathKey.size()));
    entry.baseHash = QByteArray::fromHex(base->sliced(kBaseKey.size()).toByteArray());
    entry.text = QString::fromUtf8(rest);
    if (entry.notePath.isEmpty())
        return std::unexpected(QStringLiteral("Journal entry without a note path"));
    return entry;
}

std::expected<void, QString> Journal::write(const QString& vaultPath, const QString& notePath,
                                            const QByteArray& baseHash, const QString& text)
{
    if (!QDir().mkpath(directory(vaultPath)))
        return std::unexpected(QStringLiteral("Cannot create %1").arg(directory(vaultPath)));
    return AtomicWriter::write(fileFor(vaultPath, notePath), serialise(notePath, baseHash, text));
}

void Journal::remove(const QString& vaultPath, const QString& notePath)
{
    QFile::remove(fileFor(vaultPath, notePath));
}

QList<Journal::Entry> Journal::entries(const QString& vaultPath)
{
    QList<Entry> entries;
    const QFileInfoList files = QDir(directory(vaultPath)).entryInfoList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        auto entry = parse(file.readAll());
        if (!entry)
            continue;
        entry->written = info.lastModified();
        entry->file = info.filePath();
        entries.append(*entry);
    }
    return entries;
}

QList<Journal::Entry> Journal::recoverable(const QString& vaultPath)
{
    QList<Entry> result;
    for (const Entry& entry : entries(vaultPath)) {
        const QFileInfo note(entry.notePath);
        if (note.exists()) {
            QFile file(entry.notePath);
            const bool sameAsDisk = file.open(QIODevice::ReadOnly) && file.readAll() == entry.text.toUtf8();
            if (sameAsDisk) {
                QFile::remove(entry.file); // nothing to recover
                continue;
            }
        }
        Entry offered = entry;
        offered.noteChangedSince = note.exists() && note.lastModified() > entry.written;
        result.append(offered);
    }
    return result;
}

} // namespace coeus
