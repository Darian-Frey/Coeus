#include "index/Indexer.hpp"

#include "index/ContentHash.hpp"
#include "index/LinkResolver.hpp"
#include "index/LintResolver.hpp"
#include "index/ProjectResolver.hpp"
#include "parser/RegisterParser.hpp"
#include "parser/NoteParser.hpp"
#include "vault/Vault.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace coeus {

namespace {

void collect(const QString& rootPath, const QString& relativeDir, const Indexer::IgnoreFn& ignore, QStringList& out)
{
    const QDir dir(relativeDir.isEmpty() ? rootPath : rootPath + QLatin1Char('/') + relativeDir);
    // No QDir::Hidden: dot-files and dot-folders are never notes.
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString relative =
            relativeDir.isEmpty() ? entry.fileName() : relativeDir + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!entry.isSymLink() && !(ignore && ignore(relative, true)))
                collect(rootPath, relative, ignore, out);
        } else if (entry.isFile() && Vault::isNote(entry.fileName()) && !(ignore && ignore(relative, false))) {
            out.append(relative);
        }
    }
}

// A note path noteFiles() would list: Markdown, no hidden segment, and
// neither the file nor any folder above it ignored.
bool isIndexable(const QString& relativePath, const Indexer::IgnoreFn& ignore)
{
    if (!Vault::isNote(relativePath))
        return false;
    const QStringList segments = relativePath.split(QLatin1Char('/'));
    if (std::any_of(segments.begin(), segments.end(),
                    [](const QString& s) { return s.isEmpty() || s.startsWith(QLatin1Char('.')); }))
        return false;
    if (!ignore)
        return true;
    QString prefix;
    for (qsizetype i = 0; i + 1 < segments.size(); ++i) {
        prefix = prefix.isEmpty() ? segments[i] : prefix + QLatin1Char('/') + segments[i];
        if (ignore(prefix, true))
            return false;
    }
    return !ignore(relativePath, false);
}

} // namespace

Indexer::Indexer(IndexStore& store)
    : m_store(store)
{
}

QStringList Indexer::noteFiles(const QString& rootPath, const IgnoreFn& ignore)
{
    QStringList files;
    if (QFileInfo(rootPath).isDir())
        collect(rootPath, QString(), ignore, files);
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
        if (m_rechunk) {
            if (auto chunked = putChunks(root, relativePath, content); !chunked)
                return std::unexpected(chunked.error());
        }
        return Outcome::Unchanged;
    }
    ParsedNote parsed = NoteParser::parse(content);
    const QString fileName = QFileInfo(relativePath).fileName();
    if (fileName == QLatin1String("README.md"))
        parsed.statusHeader = RegisterParser::statusHeader(QString::fromUtf8(content));
    if (RegisterParser::isRegisterFile(fileName))
        parsed.registers = RegisterParser::parse(QString::fromUtf8(content));
    if (auto put = m_store.putNote(record, parsed); !put)
        return std::unexpected(put.error());
    if (auto chunked = putChunks(root, relativePath, content); !chunked)
        return std::unexpected(chunked.error());
    return knownHash ? Outcome::Updated : Outcome::Added;
}

std::expected<void, QString> Indexer::putChunks(const RootInfo& root, const QString& relativePath, const QByteArray& content)
{
    // The passages semantic search embeds (F-018); the embedding itself happens elsewhere.
    const auto chunks = Chunker::split(QString::fromUtf8(content), IndexStore::titleOf(relativePath), m_chunkTokens, m_chunkOverlap,
                                       RegisterParser::isRegisterFile(QFileInfo(relativePath).fileName()));
    return m_store.putChunks(root.id, relativePath, chunks);
}

std::expected<Indexer::ScanReport, QString> Indexer::scan(const RootInfo& root, const IgnoreFn& ignore)
{
    auto transaction = Transaction::begin(m_store.database(), Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());
    if (auto put = m_store.putRoot(root); !put)
        return std::unexpected(put.error());

    auto known = m_store.noteHashes(root.id);
    if (!known)
        return std::unexpected(known.error());

    // Chunked under other rules (an older Coeus, other config): chunk every note again.
    const QString chunkingKey = QStringLiteral("chunking:%1").arg(root.id);
    const QString signature = Chunker::signature(m_chunkTokens, m_chunkOverlap);
    {
        auto recorded = m_store.database().prepare("SELECT value FROM meta WHERE key = ?");
        if (!recorded)
            return std::unexpected(recorded.error());
        recorded->bind(1, chunkingKey);
        m_rechunk = !recorded->step().value_or(false) || recorded->text(0) != signature;
    }

    ScanReport report;
    for (const QString& path : noteFiles(root.path, ignore)) {
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
    if (auto projects = ProjectResolver::resolveAll(m_store.database()); !projects)
        return std::unexpected(projects.error());
    if (auto lint = LintResolver::resolveAll(m_store.database()); !lint)
        return std::unexpected(lint.error());
    if (m_rechunk) {
        auto record = m_store.database().prepare("INSERT OR REPLACE INTO meta(key, value) VALUES (?, ?)");
        if (!record)
            return std::unexpected(record.error());
        if (auto done = record->bind(1, chunkingKey).bind(2, signature).run(); !done)
            return std::unexpected(done.error());
        m_rechunk = false;
    }
    if (auto committed = transaction->commit(); !committed)
        return std::unexpected(committed.error());
    return report;
}

std::expected<void, QString> Indexer::update(const RootInfo& root, const QStringList& relativePaths,
                                             const IgnoreFn& ignore)
{
    auto transaction = Transaction::begin(m_store.database(), Transaction::Kind::Immediate);
    if (!transaction)
        return std::unexpected(transaction.error());

    auto known = m_store.noteHashes(root.id);
    if (!known)
        return std::unexpected(known.error());

    bool changed = false;
    for (const QString& path : relativePaths) {
        const auto it = known->constFind(path);
        const bool indexed = it != known->constEnd();
        if (!isIndexable(path, ignore)) {
            // Not a note (any more): an ignored or hidden path drops its rows.
            if (indexed) {
                if (auto removed = m_store.removeNote(root.id, path); !removed)
                    return std::unexpected(removed.error());
                changed = true;
            }
            continue;
        }
        const auto outcome = indexFile(root, path, indexed ? &*it : nullptr);
        if (!outcome)
            return std::unexpected(outcome.error());
        changed |= *outcome == Outcome::Added || *outcome == Outcome::Updated || *outcome == Outcome::Removed;
    }
    if (changed) {
        if (auto resolved = LinkResolver::resolveAll(m_store.database()); !resolved)
            return std::unexpected(resolved.error());
        if (auto projects = ProjectResolver::resolveAll(m_store.database()); !projects)
            return std::unexpected(projects.error());
        if (auto lint = LintResolver::resolveAll(m_store.database()); !lint)
            return std::unexpected(lint.error());
    }
    return transaction->commit();
}

std::expected<Indexer::VerifyReport, QString> Indexer::verify(const RootInfo& root, const IgnoreFn& ignore)
{
    auto known = m_store.noteHashes(root.id);
    if (!known)
        return std::unexpected(known.error());

    VerifyReport report;
    for (const QString& path : noteFiles(root.path, ignore)) {
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
