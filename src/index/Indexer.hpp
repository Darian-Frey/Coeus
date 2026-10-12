#pragma once

#include "index/IndexStore.hpp"

#include <QStringList>

#include <expected>
#include <functional>

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

    // True for root-relative paths the root's .gitignore excludes (GitIgnore).
    using IgnoreFn = std::function<bool(const QString& relativePath, bool isDir)>;

    explicit Indexer(IndexStore& store);

    // Full pass: indexes new and changed notes, drops deleted and newly
    // ignored ones, resolves links.
    std::expected<ScanReport, QString> scan(const RootInfo& root, const IgnoreFn& ignore = {});
    // Files were created, changed or deleted: one transaction, one resolve.
    std::expected<void, QString> update(const RootInfo& root, const QStringList& relativePaths,
                                        const IgnoreFn& ignore = {});
    std::expected<void, QString> update(const RootInfo& root, const QString& relativePath)
    {
        return update(root, QStringList{relativePath});
    }
    // Compares the index with the disk without changing anything ("Verify index").
    std::expected<VerifyReport, QString> verify(const RootInfo& root, const IgnoreFn& ignore = {});

    // Root-relative paths of every note under `rootPath`, sorted. Skips hidden
    // files and folders (.coeus/, .git/, temporary files), folder symlinks and
    // anything `ignore` excludes.
    static QStringList noteFiles(const QString& rootPath, const IgnoreFn& ignore = {});

private:
    enum class Outcome { Added, Updated, Unchanged, Removed, Failed };
    std::expected<Outcome, QString> indexFile(const RootInfo& root, const QString& relativePath,
                                              const QByteArray* knownHash);

    IndexStore& m_store;
};

} // namespace coeus
