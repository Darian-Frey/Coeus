#pragma once

#include <QString>

#include <expected>
#include <optional>

namespace coeus {

// Read-only questions the UI asks of the index (ARCHITECTURE.md: the UI
// thread holds read-only connections). Each call opens and closes its own
// connection, so a Rebuild Index on the worker can never leave it pointing at
// a deleted file. Every query is a single indexed lookup.
class IndexQueries {
public:
    struct LinkTarget {
        QString absolutePath; // empty when unresolved or ambiguous
        bool ambiguous = false;
    };

    // How the index resolved the wikilink `target` written in the note at
    // `sourcePath` (SPEC.md §Wikilink resolution). nullopt if the index has no
    // such link, e.g. it has not caught up with an unsaved edit yet.
    static std::expected<std::optional<LinkTarget>, QString> wikilinkTarget(const QString& indexPath,
                                                                           const QString& sourcePath,
                                                                           const QString& target);
};

} // namespace coeus
