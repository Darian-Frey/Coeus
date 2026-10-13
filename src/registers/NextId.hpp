#pragma once

#include <QString>

#include <expected>

namespace coeus {

// Allocates the next free register ID in a project (SPEC.md §ID allocation,
// AV-003). Shared by the GUI and, later, the MCP server: both open index.db,
// and BEGIN IMMEDIATE serialises them. Existing IDs come from the register
// files on disk, not the index, which can lag them; unexpired reservations
// and any unsaved buffer of the register file count too. The ID is then
// reserved for kReservationSeconds. Opens a write transaction that may wait
// on the indexer: call it off the UI thread (AV-005).
class NextId {
public:
    static constexpr int kReservationSeconds = 60;

    struct Request {
        QString indexPath;
        QString rootPath;    // absolute root of the project
        QString projectPath; // root-relative project folder ("" at the root)
        QString kind;        // F C D AV BUG IMP
        QString holder = QStringLiteral("gui");
        QString unsavedText; // an open buffer of the register file, if any
    };

    struct Allocation {
        QString ident;        // e.g. "D-020"
        int number = 0;
        QString registerFile; // absolute: the existing register file, or where to create one
        bool fileExists = false;
    };

    static std::expected<Allocation, QString> allocate(const Request& request);
    // The highest `kind` number in `text`, or 0. Pure.
    static int highest(const QString& text, const QString& kind);
};

} // namespace coeus
