#pragma once

#include <QList>
#include <QString>

#include <expected>

namespace coeus {

// Starred notes and searches (F-014, SPEC.md §Bookmarks and recent notes),
// in the order starred, kept per machine in `<vault>/.coeus/bookmarks.json`.
// A note is named by its root ("vault" or a linked root's name) and its path
// within it, so a bookmark survives the root moving on disk.
struct Bookmark {
    enum class Kind { Note, Search };
    Kind kind = Kind::Note;
    QString root;  // a note's root
    QString path;  // a note's path within its root
    QString query; // a search's text
    QString mode;  // a search's mode: "words", "meaning" or "both"

    static Bookmark note(const QString& root, const QString& path) { return {Kind::Note, root, path, {}, {}}; }
    static Bookmark search(const QString& query, const QString& mode) { return {Kind::Search, {}, {}, query, mode}; }
    bool operator==(const Bookmark& other) const = default;
};

class Bookmarks {
public:
    static QString path(const QString& vaultPath);
    // The vault's bookmarks; none if the file is missing. Entries it cannot
    // read are skipped, never the whole file.
    static QList<Bookmark> load(const QString& vaultPath);
    // Written atomically (AtomicWriter).
    static std::expected<void, QString> save(const QString& vaultPath, const QList<Bookmark>& bookmarks);
    // `bookmarks` with `bookmark` added at the end, or removed if present.
    static QList<Bookmark> toggled(QList<Bookmark> bookmarks, const Bookmark& bookmark);
};

} // namespace coeus
