#include "index/Indexer.hpp"

#include "index/ContentHash.hpp"
#include "index/LinkResolver.hpp"
#include "parser/NoteParser.hpp"
#include "vault/Vault.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace coeus {

namespace {

void collect(const QString& rootPath, const QString& relativeDir, QStringList& out)
{
    const QDir dir(relativeDir.isEmpty() ? rootPath : rootPath + QLatin1Char('/') + relativeDir);
    // No QDir::Hidden: dot-files and dot-folders are never notes.
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString relative =
            relativeDir.isEmpty() ? entry.fileName() : relativeDir + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!entry.isSymLink())
                collect(rootPath, relative, out);
        } else if (entry.isFile() && Vault::isNote(entry.fileName())) {
            out.append(relative);
        }
    }
}

// A note path noteFiles() would list: Markdown, with no hidden segment.
bool isIndexable(const QString& relativePath)
{
    if (!Vault::isNote(relativePath))
        return false;
    const QStringList segments = relativePath.split(QLatin1Char('/'));
    return std::none_of(segments.begin(), segments.end(),
                        [](const QString& s) { return s.isEmpty() || s.startsWith(QLatin1Char('.')); });
}

} // namespace

Indexer::Indexer(IndexStore& store)
    : m_store(store)
{
}

QStringList Indexer::noteFiles(const QString& rootPath)
{
    QStringList files;
    if (QFileInfo(rootPath).isDir())
        collect(rootPath, QString(), files);
    files.sort();
    return files;
}

std::expected<Indexer::Outcome, QString> Indexer::indexFile(const RootInfo& root, const QString& relativePath,
                                                            const QByteArray* knownHash)
{
    const QString absolute = root.path + QLatin1Char('/') + relativePath;
    QFile file(absolute);
    const QFileInfo info(absolute);
    if (!info.isFile()) {
        if (!knownHash)
            return Outcome::Unchanged;
        if (auto removed = m_store.removeNote(root.id, relativePath); !removed)
            return std::unexpected(removed.error());
        return Outcome::Removed;
    }
    if (!file.open(QIODevice::ReadOnly))
        return Outcome::Failed;

    const QByteArray content = file.readAll();
    const NoteRecord record{root.id, relativePath, info.lastModified().toSecsSinceEpoch(), content.size(),
                            ContentHash::of(content)};

    if (knownHash && *knownHash == record.hash) {
        // Same content: keep the rows, refresh mtime and size.
        if (auto touched = m_store.touchNote(record); !touched)
            return std::unexpected(touched.error());
        return Outcome::Unchanged;
    }
    if (auto put = m_store.putNote(record, NoteParser::parse(content)); !put)
        return std::unexpected(put.error());
    return knownHash ? Outcome::Updated : Outcome::Added;
}

std::expected<Indexer::ScanReport, QString> Indexer::scan(const RootInfo& root)
{
    auto transaction = Transaction::begin(m_store.database(), Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    if (auto put = m_store.putRoot(root); !put)
        return std::unexpected(put.error());

    auto known = m_store.noteHashes(root.id);
    if (!known)
        return std::unexpected(known.error());

    ScanReport report;
    for (const QString& path : noteFiles(root.path)) {
        const auto it = known->constFind(path);
        const auto outcome = indexFile(root, path, it == known->constEnd() ? nullptr : &*it);
        if (!outcome)
            return std::unexpected(outcome.error());
        switch (*outcome) {
        case Outcome::Added: ++report.added; break;
        case Outcome::Updated: ++report.updated; break;
        case Outcome::Unchanged: ++report.unchanged; break;
        case Outcome::Removed: ++report.removed; break;
        case Outcome::Failed: report.failures.append(path); break;
        }
        known->remove(path);
    }
    for (auto it = known->constBegin(); it != known->constEnd(); ++it) {
        if (auto removed = m_store.removeNote(root.id, it.key()); !removed)
            return std::unexpected(removed.error());
        ++report.removed;
    }

    if (auto resolved = LinkResolver::resolveAll(m_store.database()); !resolved)
        return std::unexpected(resolved.error());
    if (auto committed = transaction->commit(); !committed)
        return std::unexpected(committed.error());
    return report;
}

std::expected<void, QString> Indexer::update(const RootInfo& root, const QString& relativePath)
{
    auto transaction = Transaction::begin(m_store.database(), Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());

    auto known = m_store.noteHashes(root.id);
    if (!known)
        return std::unexpected(known.error());
    const auto it = known->constFind(relativePath);

    if (isIndexable(relativePath) || it != known->constEnd()) {
        const auto outcome = indexFile(root, relativePath, it == known->constEnd() ? nullptr : &*it);
        if (!outcome)
            return std::unexpected(outcome.error());
        if (*outcome == Outcome::Unchanged || *outcome == Outcome::Failed)
            return transaction->commit();
    }
    if (auto resolved = LinkResolver::resolveAll(m_store.database()); !resolved)
        return std::unexpected(resolved.error());
    return transaction->commit();
}

std::expected<Indexer::VerifyReport, QString> Indexer::verify(const RootInfo& root)
{
    auto known = m_store.noteHashes(root.id);
    if (!known)
        return std::unexpected(known.error());

    VerifyReport report;
    for (const QString& path : noteFiles(root.path)) {
        const auto it = known->constFind(path);
        if (it == known->constEnd()) {
            report.notIndexed.append(path);
            continue;
        }
        QFile file(root.path + QLatin1Char('/') + path);
        if (file.open(QIODevice::ReadOnly) && ContentHash::of(file.readAll()) != *it)
            report.stale.append(path);
        known->remove(path);
    }
    report.notOnDisk = known->keys();
    report.notOnDisk.sort();
    return report;
}

} // namespace coeus
