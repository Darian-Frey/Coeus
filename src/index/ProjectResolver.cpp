#include "index/ProjectResolver.hpp"

#include <QHash>
#include <QList>
#include <QPair>

#include <cstdint>

namespace coeus {

namespace {

#define COEUS_TRY(expr)                                                                                                \
    do {                                                                                                               \
        if (auto coeusResult = (expr); !coeusResult)                                                                   \
            return std::unexpected(coeusResult.error());                                                               \
    } while (false)

QString folderOf(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

struct Header {
    QString status, provenance, lastReviewed, why;
};

using Key = QPair<int, QString>; // (root, project folder)

} // namespace

std::expected<ProjectResolver::Report, QString> ProjectResolver::resolveAll(Database& db)
{
    Report report;

    // 1. The projects the status headers declare.
    QHash<Key, Header> wanted;
    {
        auto rows = db.prepare("SELECT n.root_id, n.path, h.status, h.provenance, h.last_reviewed, h.why "
                               "FROM status_headers h JOIN notes n ON n.id = h.note");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            wanted.insert({int(rows->int64(0)), folderOf(rows->text(1))},
                          {rows->text(2), rows->text(3), rows->text(4), rows->text(5)});
        }
    }

    // 2. Reconcile with the projects table, keeping ids.
    QHash<Key, std::int64_t> ids;
    {
        auto rows = db.prepare("SELECT id, root_id, path, status, provenance, last_reviewed, why FROM projects");
        if (!rows)
            return std::unexpected(rows.error());
        QList<std::int64_t> gone;
        QList<QPair<std::int64_t, Header>> stale;
        while (rows->step().value_or(false)) {
            const Key key{int(rows->int64(1)), rows->text(2)};
            const std::int64_t id = rows->int64(0);
            const auto it = wanted.constFind(key);
            if (it == wanted.constEnd()) {
                gone.append(id);
                continue;
            }
            ids.insert(key, id);
            if (rows->text(3) != it->status || rows->text(4) != it->provenance || rows->text(5) != it->lastReviewed
                || rows->text(6) != it->why)
                stale.append({id, *it});
        }
        for (const std::int64_t id : std::as_const(gone)) {
            for (const char* sql : {"UPDATE notes SET project_id = NULL WHERE project_id = ?",
                                    "DELETE FROM projects WHERE id = ?"}) {
                auto statement = db.prepare(sql);
                if (!statement)
                    return std::unexpected(statement.error());
                COEUS_TRY(statement->bind(1, id).run());
            }
            ++report.changed;
        }
        auto update = db.prepare("UPDATE projects SET status = ?, provenance = ?, last_reviewed = ?, why = ? WHERE id = ?");
        if (!update)
            return std::unexpected(update.error());
        for (const auto& [id, header] : std::as_const(stale)) {
            update->reset();
            COEUS_TRY(update->bind(1, header.status).bind(2, header.provenance).bind(3, header.lastReviewed)
                          .bind(4, header.why).bind(5, id).run());
            ++report.changed;
        }
        auto insert = db.prepare("INSERT INTO projects(root_id, path, status, provenance, last_reviewed, why) VALUES (?, ?, ?, ?, ?, ?)");
        if (!insert)
            return std::unexpected(insert.error());
        for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it) {
            if (ids.contains(it.key()))
                continue;
            insert->reset();
            COEUS_TRY(insert->bind(1, it.key().first).bind(2, it.key().second).bind(3, it->status)
                          .bind(4, it->provenance).bind(5, it->lastReviewed).bind(6, it->why).run());
            ids.insert(it.key(), db.lastInsertRowId());
            ++report.changed;
        }
    }
    report.projects = int(ids.size());

    // 3. Each note belongs to the nearest project folder above it in its root.
    {
        auto rows = db.prepare("SELECT id, root_id, path, project_id FROM notes");
        if (!rows)
            return std::unexpected(rows.error());
        QList<QPair<std::int64_t, std::int64_t>> changes; // note, project (0 for none)
        while (rows->step().value_or(false)) {
            const int root = int(rows->int64(1));
            QString folder = folderOf(rows->text(2));
            std::int64_t project = 0;
            for (;;) {
                if (const auto it = ids.constFind({root, folder}); it != ids.constEnd()) {
                    project = *it;
                    break;
                }
                if (folder.isEmpty())
                    break;
                folder = folderOf(folder);
            }
            const std::int64_t current = rows->isNull(3) ? 0 : rows->int64(3);
            if (current != project)
                changes.append({rows->int64(0), project});
        }
        auto update = db.prepare("UPDATE notes SET project_id = ? WHERE id = ?");
        if (!update)
            return std::unexpected(update.error());
        for (const auto& [note, project] : std::as_const(changes)) {
            update->reset();
            if (project)
                update->bind(1, project);
            else
                update->bindNull(1);
            COEUS_TRY(update->bind(2, note).run());
        }
        report.changed += int(changes.size());
    }

    // 4. Register entries follow their note; 5. citations resolve within a project
    //    (to the first entry with that ident by path and line, should one be
    //    duplicated: AV-003; never by row id, which differs after a rebuild).
    auto attach = db.prepare("UPDATE registers SET project_id = (SELECT project_id FROM notes WHERE notes.id = registers.note) "
                             "WHERE project_id IS NOT (SELECT project_id FROM notes WHERE notes.id = registers.note)");
    if (!attach)
        return std::unexpected(attach.error());
    COEUS_TRY(attach->run());
    report.changed += db.changes();

    auto cite = db.prepare(
        "UPDATE register_refs SET target = ("
        "  SELECT t.id FROM registers s JOIN registers t ON t.project_id = s.project_id AND t.ident = register_refs.target_ident"
        "  WHERE s.id = register_refs.src AND s.project_id IS NOT NULL"
        "  ORDER BY (SELECT path FROM notes WHERE notes.id = t.note), t.line LIMIT 1) "
        "WHERE target IS NOT ("
        "  SELECT t.id FROM registers s JOIN registers t ON t.project_id = s.project_id AND t.ident = register_refs.target_ident"
        "  WHERE s.id = register_refs.src AND s.project_id IS NOT NULL"
        "  ORDER BY (SELECT path FROM notes WHERE notes.id = t.note), t.line LIMIT 1)");
    if (!cite)
        return std::unexpected(cite.error());
    COEUS_TRY(cite->run());
    report.changed += db.changes();

    auto count = db.prepare("SELECT count(*) FROM registers WHERE project_id IS NOT NULL");
    if (count && count->step().value_or(false))
        report.registerEntries = int(count->int64(0));
    return report;
}

} // namespace coeus
