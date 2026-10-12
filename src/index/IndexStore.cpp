#include "index/IndexStore.hpp"

#include "index/Schema.hpp"

#include <QFile>
#include <QFileInfo>

namespace coeus {

namespace {

// Propagates the error of an expected-returning call.
#define COEUS_TRY(expr)                                                                                                \
    do {                                                                                                               \
        if (auto coeusResult = (expr); !coeusResult)                                                                   \
            return std::unexpected(coeusResult.error());                                                               \
    } while (false)

const char* typeName(PropertyType type)
{
    switch (type) {
    case PropertyType::Number:
        return "number";
    case PropertyType::Date:
        return "date";
    case PropertyType::List:
        return "list";
    case PropertyType::Text:
        break;
    }
    return "text";
}

bool isAliasKey(const QString& key)
{
    return key == QLatin1String("aliases") || key == QLatin1String("alias");
}

void removeDatabaseFiles(const QString& path)
{
    for (const char* suffix : {"", "-wal", "-shm"})
        QFile::remove(path + QLatin1String(suffix));
}

} // namespace

IndexStore::IndexStore(Database db)
    : m_db(std::move(db))
{
}

std::expected<IndexStore, QString> IndexStore::open(const QString& path)
{
    QString lastError;
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto db = Database::open(path);
        if (!db) {
            // Corrupt or not a database: discard the cache and start again.
            lastError = db.error();
            if (path == QLatin1String(":memory:"))
                break;
            removeDatabaseFiles(path);
            continue;
        }

        const auto version = Schema::version(*db);
        if (version && *version == Schema::kVersion)
            return IndexStore(std::move(*db));
        if (version && *version == 0) {
            COEUS_TRY(Schema::create(*db));
            return IndexStore(std::move(*db));
        }
        // Another version, or unreadable: discard the cache and start again.
        lastError = version ? QStringLiteral("schema version %1").arg(*version) : version.error();
        if (path == QLatin1String(":memory:"))
            break;
        *db = Database::open(QStringLiteral(":memory:")).value(); // release the file before deleting it
        removeDatabaseFiles(path);
    }
    return std::unexpected(QStringLiteral("Cannot create index at %1: %2").arg(path, lastError));
}

QString IndexStore::titleOf(const QString& path)
{
    return QFileInfo(path).completeBaseName();
}

std::expected<void, QString> IndexStore::putRoot(const RootInfo& root)
{
    auto statement = m_db.prepare("INSERT INTO roots(id, name, path, present) VALUES (?, ?, ?, 1) "
                                  "ON CONFLICT(id) DO UPDATE SET name=excluded.name, path=excluded.path, present=1");
    if (!statement)
        return std::unexpected(statement.error());
    return statement->bind(1, root.id).bind(2, root.name).bind(3, root.path).run();
}

std::expected<QHash<QString, QByteArray>, QString> IndexStore::noteHashes(int rootId)
{
    auto statement = m_db.prepare("SELECT path, hash FROM notes WHERE root_id = ?");
    if (!statement)
        return std::unexpected(statement.error());
    statement->bind(1, rootId);
    QHash<QString, QByteArray> hashes;
    for (;;) {
        const auto row = statement->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return hashes;
        hashes.insert(statement->text(0), statement->blob(1));
    }
}

std::expected<std::int64_t, QString> IndexStore::noteId(int rootId, const QString& path)
{
    auto statement = m_db.prepare("SELECT id FROM notes WHERE root_id = ? AND path = ?");
    if (!statement)
        return std::unexpected(statement.error());
    statement->bind(1, rootId).bind(2, path);
    const auto row = statement->step();
    if (!row)
        return std::unexpected(row.error());
    return *row ? statement->int64(0) : 0;
}

std::expected<void, QString> IndexStore::removeNote(int rootId, const QString& path)
{
    const auto id = noteId(rootId, path);
    if (!id)
        return std::unexpected(id.error());
    if (*id == 0)
        return {};

    auto fts = m_db.prepare("DELETE FROM fts WHERE rowid = ?");
    if (!fts)
        return std::unexpected(fts.error());
    COEUS_TRY(fts->bind(1, *id).run());

    auto note = m_db.prepare("DELETE FROM notes WHERE id = ?"); // cascades to every derived row
    if (!note)
        return std::unexpected(note.error());
    return note->bind(1, *id).run();
}

std::expected<void, QString> IndexStore::touchNote(const NoteRecord& record)
{
    auto statement = m_db.prepare("UPDATE notes SET mtime = ?, size = ? WHERE root_id = ? AND path = ?");
    if (!statement)
        return std::unexpected(statement.error());
    return statement->bind(1, record.mtime).bind(2, record.size).bind(3, record.rootId).bind(4, record.path).run();
}

std::expected<void, QString> IndexStore::putNote(const NoteRecord& record, const ParsedNote& parsed)
{
    // Keep the note's id across a replace so links into it stay attached.
    const auto existing = noteId(record.rootId, record.path);
    if (!existing)
        return std::unexpected(existing.error());
    COEUS_TRY(removeNote(record.rootId, record.path));

    auto note = m_db.prepare("INSERT INTO notes(id, root_id, path, title, mtime, size, hash) VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!note)
        return std::unexpected(note.error());
    if (*existing != 0)
        note->bind(1, *existing);
    else
        note->bindNull(1);
    const QString title = titleOf(record.path);
    COEUS_TRY(note->bind(2, record.rootId)
                  .bind(3, record.path)
                  .bind(4, title)
                  .bind(5, record.mtime)
                  .bind(6, record.size)
                  .bind(7, record.hash)
                  .run());
    const std::int64_t id = m_db.lastInsertRowId();

    auto link = m_db.prepare("INSERT INTO links(src, target_raw, style, heading, block, alias, line, embed) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    if (!link)
        return std::unexpected(link.error());
    for (const Link& l : parsed.links) {
        link->reset();
        COEUS_TRY(link->bind(1, id)
                      .bind(2, l.target)
                      .bind(3, QString::fromLatin1(l.style == LinkStyle::Wiki ? "wiki" : "markdown"))
                      .bind(4, l.heading.isEmpty() ? QString() : l.heading)
                      .bind(5, l.block.isEmpty() ? QString() : l.block)
                      .bind(6, l.alias.isEmpty() ? QString() : l.alias)
                      .bind(7, l.line)
                      .bind(8, int(l.embed))
                      .run());
    }

    auto tag = m_db.prepare("INSERT INTO tags(note, tag, line) VALUES (?, ?, ?)");
    if (!tag)
        return std::unexpected(tag.error());
    for (const Tag& t : parsed.tags) {
        tag->reset();
        COEUS_TRY(tag->bind(1, id).bind(2, t.name).bind(3, t.line).run());
    }

    auto prop = m_db.prepare("INSERT INTO props(note, key, value, type, line) VALUES (?, ?, ?, ?, ?)");
    if (!prop)
        return std::unexpected(prop.error());
    for (const Property& p : parsed.properties) {
        // Aliases are stored already split, as the resolver reads them.
        const QStringList values = isAliasKey(p.key) ? parsed.aliases : p.values;
        const char* type = isAliasKey(p.key) ? "list" : typeName(p.type);
        for (const QString& value : values) {
            prop->reset();
            COEUS_TRY(prop->bind(1, id)
                          .bind(2, p.key)
                          .bind(3, value)
                          .bind(4, QString::fromLatin1(type))
                          .bind(5, p.line)
                          .run());
        }
    }

    auto heading = m_db.prepare("INSERT INTO headings(note, level, text, line) VALUES (?, ?, ?, ?)");
    if (!heading)
        return std::unexpected(heading.error());
    for (const Heading& h : parsed.headings) {
        heading->reset();
        COEUS_TRY(heading->bind(1, id).bind(2, h.level).bind(3, h.text).bind(4, h.line).run());
    }

    auto block = m_db.prepare("INSERT INTO blocks(note, block_id, line) VALUES (?, ?, ?)");
    if (!block)
        return std::unexpected(block.error());
    for (const BlockId& b : parsed.blocks) {
        block->reset();
        COEUS_TRY(block->bind(1, id).bind(2, b.id).bind(3, b.line).run());
    }

    auto fts = m_db.prepare("INSERT INTO fts(rowid, title, body, path) VALUES (?, ?, ?, ?)");
    if (!fts)
        return std::unexpected(fts.error());
    return fts->bind(1, id).bind(2, title).bind(3, parsed.plainText).bind(4, record.path).run();
}

} // namespace coeus
