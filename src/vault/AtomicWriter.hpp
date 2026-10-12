#pragma once

#include <QByteArrayView>
#include <QString>

#include <expected>

namespace coeus {

// The only way Coeus writes a vault file (ARCHITECTURE.md invariant 8, AV-002).
// Writes to a temporary sibling, fsyncs it, renames it over the target and
// fsyncs the directory, so a crash leaves either the old or the new content,
// never a mixture (SPEC.md §Atomic write).
class AtomicWriter {
public:
    // Marker in temporary file names; the sync layer excludes "*.coeus-tmp-*".
    static constexpr const char* kTempMarker = ".coeus-tmp-";

    // Replaces `path` with `content`, creating it if absent. If `path` is a
    // symlink, the link's target is replaced and the link is kept. An existing
    // file's mode is kept; its owner and group are kept where permitted.
    // The parent directory must already exist.
    static std::expected<void, QString> write(const QString& path, QByteArrayView content);
};

} // namespace coeus
