#include "index/IndexQueries.hpp"

#include "index/Database.hpp"
#include "registers/Lint.hpp"

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
        "         WHERE p.note = n.id AND p.key IN ('aliases', 'alias')), "
        "       (SELECT path FROM projects WHERE projects.id = n.project_id) "
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
        NoteEntry note{query->text(0), query->text(1), query->text(2), query->text(3), {}, {}};
        if (!query->isNull(4))
            note.aliases = query->text(4).split(QChar(31), Qt::SkipEmptyParts);
        note.project = query->isNull(5) ? QString() : query->text(5);
        notes.append(note);
    }
}

std::expected<QList<RegisterRef>, QString> IndexQueries::registers(const QString& indexPath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto query = db->prepare(
        "SELECT r.name, r.path, p.path, g.kind, g.number, g.ident, g.title, g.status, r.path || '/' || n.path, g.line "
        "FROM registers g JOIN projects p ON p.id = g.project_id JOIN notes n ON n.id = g.note "
        "JOIN roots r ON r.id = n.root_id ORDER BY r.id, p.path, g.kind, g.number, n.path, g.line");
    if (!query)
        return std::unexpected(query.error());
    QList<RegisterRef> refs;
    for (;;) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return refs;
        refs.append({query->text(0), query->text(1), query->text(2), query->text(3), int(query->int64(4)),
                     query->text(5), query->text(6), query->isNull(7) ? QString() : query->text(7), query->text(8),
                     int(query->int64(9))});
    }
}

std::expected<QList<ProblemRef>, QString> IndexQueries::problems(const QString& indexPath, const QDate& today,
                                                                 int reviewThresholdDays)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    struct Sortable {
        std::int64_t root;
        QString path;
        ProblemRef problem;
    };
    QList<Sortable> rows;
    auto stored = db->prepare("SELECT r.id, n.path, r.name, r.path, l.rule, l.level, coalesce(l.line, 0), l.message, l.subject "
                              "FROM lint l JOIN notes n ON n.id = l.note JOIN roots r ON r.id = n.root_id");
    if (!stored)
        return std::unexpected(stored.error());
    for (;;) {
        const auto row = stored->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        rows.append({stored->int64(0), stored->text(1),
                     {stored->text(2), stored->text(3), stored->text(3) + QLatin1Char('/') + stored->text(1), stored->text(4),
                      stored->text(5), int(stored->int64(6)), stored->text(7), stored->text(8)}});
    }
    // L8 depends on the date, so it is evaluated here rather than stored.
    auto headers = db->prepare("SELECT r.id, n.path, r.name, r.path, coalesce(h.last_reviewed, '') FROM status_headers h "
                               "JOIN notes n ON n.id = h.note JOIN roots r ON r.id = n.root_id");
    if (!headers)
        return std::unexpected(headers.error());
    for (;;) {
        const auto row = headers->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            break;
        const auto stale = Lint::staleReview({0, 0, QString(), headers->text(4)}, today, reviewThresholdDays);
        if (stale) {
            rows.append({headers->int64(0), headers->text(1),
                         {headers->text(2), headers->text(3), headers->text(3) + QLatin1Char('/') + headers->text(1),
                          stale->rule, stale->level, stale->line, stale->message, stale->subject}});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Sortable& a, const Sortable& b) {
        return std::tie(a.root, a.path, a.problem.line, a.problem.rule, a.problem.message)
               < std::tie(b.root, b.path, b.problem.line, b.problem.rule, b.problem.message);
    });
    QList<ProblemRef> problems;
    problems.reserve(rows.size());
    for (const Sortable& row : std::as_const(rows))
        problems.append(row.problem);
    return problems;
}

std::expected<QList<IdeaRef>, QString> IndexQueries::ideas(const QString& indexPath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto query = db->prepare("SELECT r.path || '/' || n.path, n.title, i.stage, coalesce(i.codename, ''), coalesce(i.repo, ''), "
                             "coalesce(i.summary, '') FROM ideas i JOIN notes n ON n.id = i.note JOIN roots r ON r.id = n.root_id "
                             "ORDER BY n.path");
    if (!query)
        return std::unexpected(query.error());
    QList<IdeaRef> ideas;
    for (;;) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return ideas;
        ideas.append({query->text(0), query->text(1), query->text(2), query->text(3), query->text(4), query->text(5)});
    }
}

std::expected<QList<ProjectRef>, QString> IndexQueries::projects(const QString& indexPath)
{
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());
    auto query = db->prepare("SELECT r.name, r.path, p.path, p.status, p.last_reviewed FROM projects p "
                             "JOIN roots r ON r.id = p.root_id ORDER BY r.id, p.path");
    if (!query)
        return std::unexpected(query.error());
    QList<ProjectRef> projects;
    for (;;) {
        const auto row = query->step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return projects;
        projects.append({query->text(0), query->text(1), query->text(2), query->text(3), query->text(4)});
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
