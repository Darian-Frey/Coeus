#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace coeus {

// One indexed note, as the [[ completer sees it.
struct NoteEntry {
    QString rootName;  // "vault" for root 0
    QString rootPath;  // absolute
    QString path;      // root-relative, with .md
    QString title;     // filename without extension
    QStringList aliases;
};

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

    // Every indexed note with its aliases, for the [[ completer (F-004).
    static std::expected<QList<NoteEntry>, QString> allNotes(const QString& indexPath);
    // Heading texts of the note at `notePath`, in document order.
    static std::expected<QStringList, QString> headings(const QString& indexPath, const QString& notePath);
};

} // namespace coeus
