#pragma once

#include "index/IndexStore.hpp"

#include <QStringList>

#include <expected>

namespace coeus {

// Brings the index in line with the files of a root. Synchronous and
// filesystem-bound: it runs on the indexer worker thread, never the UI thread
// (AV-005). Each call is one transaction, so a crash leaves the index as it
// was before the call (AV-001).
class Indexer {
public:
    struct ScanReport {
        int added = 0;
        int updated = 0;
        int removed = 0;
        int unchanged = 0;
        QStringList failures; // files that could not be read; their rows are kept
    };

    struct VerifyReport {
        QStringList notIndexed;  // on disk, missing from the index
        QStringList notOnDisk;   // in the index, missing from disk
        QStringList stale;       // hash differs from the file
        bool clean() const { return notIndexed.isEmpty() && notOnDisk.isEmpty() && stale.isEmpty(); }
    };

    explicit Indexer(IndexStore& store);

    // Full pass: indexes new and changed notes, drops deleted ones, resolves links.
    std::expected<ScanReport, QString> scan(const RootInfo& root);
    // One file was created, changed or deleted.
    std::expected<void, QString> update(const RootInfo& root, const QString& relativePath);
    // Compares the index with the disk without changing anything ("Verify index").
    std::expected<VerifyReport, QString> verify(const RootInfo& root);

    // Root-relative paths of every note under `rootPath`, sorted. Skips hidden
    // files and folders (.coeus/, .git/, temporary files) and folder symlinks.
    static QStringList noteFiles(const QString& rootPath);

private:
    enum class Outcome { Added, Updated, Unchanged, Removed, Failed };
    std::expected<Outcome, QString> indexFile(const RootInfo& root, const QString& relativePath,
                                              const QByteArray* knownHash);

    IndexStore& m_store;
};

} // namespace coeus
