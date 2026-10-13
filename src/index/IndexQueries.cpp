#include "index/IndexQueries.hpp"

#include "index/Database.hpp"

#include <QFileInfo>

namespace coeus {

std::expected<std::optional<IndexQueries::LinkTarget>, QString>
IndexQueries::wikilinkTarget(const QString& indexPath, const QString& sourcePath, const QString& target)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());

    auto query = db->prepare(
        "SELECT tr.path || '/' || t.path, l.ambiguous "
        "FROM links l "
        "JOIN notes s ON s.id = l.src JOIN roots sr ON sr.id = s.root_id "
        "LEFT JOIN notes t ON t.id = l.target_note LEFT JOIN roots tr ON tr.id = t.root_id "
        "WHERE sr.path || '/' || s.path = ? AND l.target_raw = ? AND l.style = 'wiki' "
        "LIMIT 1");
    if (!query)
        return std::unexpected(query.error());
    query->bind(1, sourcePath).bind(2, target);
    const auto row = query->step();
    if (!row)
        return std::unexpected(row.error());
    if (!*row)
        return std::optional<LinkTarget>();
    return std::optional<LinkTarget>(LinkTarget{query->isNull(0) ? QString() : query->text(0), query->int64(1) != 0});
}

std::expected<QList<NoteEntry>, QString> IndexQueries::allNotes(const QString& indexPath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());

    // One row per note, aliases joined with a separator no alias contains.
    auto query = db->prepare(
        "SELECT r.name, r.path, n.path, n.title, "
        "       (SELECT group_concat(p.value, char(31)) FROM props p "
        "         WHERE p.note = n.id AND p.key IN ('aliases', 'alias')) "
        "FROM notes n JOIN roots r ON r.id = n.root_id ORDER BY n.root_id, n.path");
    if (!query)
        return std::unexpected(query.error());
    QList<NoteEntry> notes;
    for (;;) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return notes;
        NoteEntry note{query->text(0), query->text(1), query->text(2), query->text(3), {}};
        if (!query->isNull(4))
            note.aliases = query->text(4).split(QChar(31), Qt::SkipEmptyParts);
        notes.append(note);
    }
}

std::expected<QStringList, QString> IndexQueries::headings(const QString& indexPath, const QString& notePath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto query = db->prepare("SELECT h.text FROM headings h JOIN notes n ON n.id = h.note JOIN roots r ON r.id = n.root_id "
                             "WHERE r.path || '/' || n.path = ? ORDER BY h.line");
    if (!query)
        return std::unexpected(query.error());
    query->bind(1, notePath);
    QStringList headings;
    for (;;) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return headings;
        headings.append(query->text(0));
    }
}

} // namespace coeus
