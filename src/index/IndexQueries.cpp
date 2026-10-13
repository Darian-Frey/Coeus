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

} // namespace coeus
