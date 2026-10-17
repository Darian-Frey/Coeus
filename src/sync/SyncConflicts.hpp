#pragma once

#include "sync/SyncProvider.hpp"

#include <QFileInfo>
#include <QList>
#include <QString>

#include <optional>

namespace coeus {

// Conflict copies in the vault (D-024): `<name>.conflictN` beside the file
// sync kept. Found by name, whichever provider made them; `.coeus/`,
// `.git/` and `.coeus-shared/` (whose sidecar unites its own copies, F-035)
// are not searched.
class SyncConflicts {
public:
    static QList<SyncConflict> find(const QString& vaultPath);
    // The kept file's name for a conflict copy's, if it is one: "note.md" for "note.md.conflict2".
    static std::optional<QString> originalOf(const QString& copyName);
    // Every file sync carries: the vault less `.coeus/`, `.git/` and Coeus's
    // temporary files, and `.coeus-shared/` only if `shared`. Never enters the excluded folders.
    static QList<QFileInfo> syncedFiles(const QString& vaultPath, bool shared);
};

} // namespace coeus
