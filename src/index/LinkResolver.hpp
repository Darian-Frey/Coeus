#pragma once

#include "index/Database.hpp"

#include <expected>

namespace coeus {

// Sets links.target_note and links.ambiguous for every link in the index
// (SPEC.md §Wikilink resolution, AV-006). Resolution depends on every note,
// since adding one can resolve or make ambiguous a link elsewhere, so the
// whole set is recomputed from memory and only changed rows are written.
// Call inside the transaction that changed the notes.
class LinkResolver {
public:
    struct Report {
        int resolved = 0;
        int unresolved = 0; // excludes file links (isFileLink)
        int ambiguous = 0;
        int changed = 0;    // rows written
    };

    static std::expected<Report, QString> resolveAll(Database& db);

    // Attachment targets (images, PDFs, media) are not notes and are left
    // unresolved without being counted as broken.
    static bool isAttachment(const QString& target);
    // Attachments, plus Markdown links to any file that is not .md (source
    // code, data). Left unresolved and not counted as broken.
    static bool isFileLink(bool markdown, const QString& target);
};

} // namespace coeus
