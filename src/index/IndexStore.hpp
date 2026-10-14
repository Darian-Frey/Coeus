#pragma once

#include "index/Database.hpp"
#include "search/Chunker.hpp"
#include "parser/ParsedNote.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <cstdint>
#include <expected>

namespace coeus {

struct RootInfo {
    int id = 0; // 0 is the vault folder (SPEC.md §Linked roots)
    QString name;
    QString path; // absolute, this machine
};

struct NoteRecord {
    int rootId = 0;
    QString path; // root-relative, forward slashes
    std::int64_t mtime = 0;
    std::int64_t size = 0;
    QByteArray hash;
};

// Row-level access to the index database. Writers call putNote/removeNote
// inside a Transaction so a note's rows change together (SPEC.md §Index schema).
class IndexStore {
public:
    // Opens `path`, creating the schema. An index with another schema version
    // is deleted and recreated, since the index is a cache (D-005).
    static std::expected<IndexStore, QString> open(const QString& path);

    // Records a root. `present` is false when its folder is missing on this
    // machine: the root's rows are kept and shown greyed (AV-010).
    std::expected<void, QString> putRoot(const RootInfo& root, bool present = true);
    // Drops a root removed from config, with every row derived from it.
    std::expected<void, QString> removeRoot(int rootId);
    // Replaces the passages of the note at (rootId, path) (F-018).
    std::expected<void, QString> putChunks(int rootId, const QString& path, const QList<Chunker::Chunk>& chunks);
    // Every recorded root, ordered by id.
    std::expected<QList<RootInfo>, QString> roots();
    std::expected<int, QString> noteCount();

    // Root-relative path -> hash for every note in `rootId`.
    std::expected<QHash<QString, QByteArray>, QString> noteHashes(int rootId);

    // Replaces every row derived from one note.
    std::expected<void, QString> putNote(const NoteRecord& record, const ParsedNote& note);
    // Records a new mtime and size for a note whose content is unchanged.
    std::expected<void, QString> touchNote(const NoteRecord& record);
    std::expected<void, QString> removeNote(int rootId, const QString& path);

    Database& database() { return m_db; }

    static QString titleOf(const QString& path);

private:
    explicit IndexStore(Database db);
    std::expected<std::int64_t, QString> noteId(int rootId, const QString& path);

    Database m_db;
};

} // namespace coeus
