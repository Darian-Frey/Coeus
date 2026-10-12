#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <chrono>

namespace coeus {

// Removes temporary files that interrupted AtomicWriter saves left behind
// (IMP-002). The one vault deletion Coeus makes without a user command, so it
// is deliberately narrow: only regular files whose names AtomicWriter
// generates, only once they are stale, and never inside .git/.
// Synchronous and filesystem-bound: call it off the UI thread (AV-005).
class TempSweeper {
public:
    // Younger temporary files may belong to a save in progress in another
    // Coeus process (coeus --mcp), so they are left alone.
    static constexpr std::chrono::seconds kMinAge{60};

    struct Result {
        int removed = 0;
        QStringList failures; // paths that matched but could not be removed
    };

    static Result sweep(const QString& root, const QDateTime& now = QDateTime::currentDateTimeUtc(),
                        std::chrono::seconds minAge = kMinAge);

    // True if `fileName` has the form AtomicWriter gives temporary files.
    static bool isTempName(const QString& fileName);
};

} // namespace coeus
