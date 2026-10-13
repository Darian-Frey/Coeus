#include "index/LintResolver.hpp"

#include "registers/Lint.hpp"

#include <QHash>
#include <QPair>

#include <map>

namespace coeus {

namespace {

QString folderOf(const QString& path)
{
    // "" rather than a null QString at the root: a null binds NULL, and
    // projects.path is NOT NULL (a root whose own README is a project).
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QStringLiteral("") : path.left(slash);
}

} // namespace

std::expected<LintResolver::Report, QString> LintResolver::resolveAll(Database& db)
{
    // 1. What the lint needs, as the index holds it.
    QList<Lint::Entry> entries;
    QHash<std::int64_t, qsizetype> indexOf;
    {
        auto rows = db.prepare("SELECT g.id, g.note, n.path, coalesce(g.project_id, 0), g.kind, g.ident, g.line "
                               "FROM registers g JOIN notes n ON n.id = g.note ORDER BY g.id");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            Lint::Entry entry;
            entry.id = rows->int64(0);
            entry.note = rows->int64(1);
            entry.notePath = rows->text(2);
            entry.project = rows->int64(3);
            entry.kind = rows->text(4);
            entry.ident = rows->text(5);
            entry.line = int(rows->int64(6));
            indexOf.insert(entry.id, entries.size());
            entries.append(entry);
        }
    }
    {
        auto rows = db.prepare("SELECT reg, name, value, line FROM register_fields ORDER BY rowid");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            const auto it = indexOf.constFind(rows->int64(0));
            if (it == indexOf.constEnd())
                continue;
            const int line = int(rows->int64(3));
            // The stored value has lost where a bare "**Name.**" put its first line.
            const QString value = rows->text(2);
            entries[*it].fields.append({rows->text(1), value, line, line});
        }
    }
    {
        auto rows = db.prepare("SELECT src, target_ident, line, coalesce(target, 0) FROM register_refs ORDER BY rowid");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            const auto it = indexOf.constFind(rows->int64(0));
            if (it != indexOf.constEnd())
                entries[*it].citations.append({rows->text(1), int(rows->int64(2)), rows->int64(3)});
        }
    }
    {
        auto rows = db.prepare("SELECT src, ident, line FROM register_mentions ORDER BY rowid");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            const auto it = indexOf.constFind(rows->int64(0));
            if (it != indexOf.constEnd())
                entries[*it].mentions.append({rows->text(1), int(rows->int64(2))});
        }
    }
    QList<Lint::Project> projects;
    {
        QHash<QPair<std::int64_t, QString>, std::int64_t> readmes; // (root, folder) -> note
        auto headers = db.prepare("SELECT n.root_id, n.path, n.id FROM status_headers h JOIN notes n ON n.id = h.note");
        if (!headers)
            return std::unexpected(headers.error());
        while (headers->step().value_or(false))
            readmes.insert({headers->int64(0), folderOf(headers->text(1))}, headers->int64(2));
        auto rows = db.prepare("SELECT id, root_id, path, status, coalesce(last_reviewed, '') FROM projects ORDER BY id");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            const std::int64_t note = readmes.value({rows->int64(1), rows->text(2)});
            if (note)
                projects.append({rows->int64(0), note, rows->text(3), rows->text(4)});
        }
    }

    // 2. Reconcile with the lint table: keep equal rows, delete the rest, add the new.
    const QList<Lint::Problem> wanted = Lint::check(entries, projects);
    Report report;
    report.problems = int(wanted.size());
    std::map<Lint::Problem, int> missing; // problem -> how many more are wanted
    for (const Lint::Problem& problem : wanted)
        ++missing[problem];
    QList<std::int64_t> stale;
    {
        auto rows = db.prepare("SELECT rowid, note, rule, level, coalesce(line, 0), message, subject FROM lint");
        if (!rows)
            return std::unexpected(rows.error());
        while (rows->step().value_or(false)) {
            const Lint::Problem existing{rows->int64(1), rows->text(2), rows->text(3), int(rows->int64(4)), rows->text(5),
                                         rows->text(6)};
            const auto it = missing.find(existing);
            if (it != missing.end() && it->second > 0)
                --it->second;
            else
                stale.append(rows->int64(0));
        }
    }
    if (!stale.isEmpty()) {
        auto remove = db.prepare("DELETE FROM lint WHERE rowid = ?");
        if (!remove)
            return std::unexpected(remove.error());
        for (const std::int64_t rowid : std::as_const(stale)) {
            remove->reset();
            if (auto done = remove->bind(1, rowid).run(); !done)
                return std::unexpected(done.error());
            ++report.changed;
        }
    }
    auto insert = db.prepare("INSERT INTO lint(note, rule, level, line, message, subject) VALUES (?, ?, ?, ?, ?, ?)");
    if (!insert)
        return std::unexpected(insert.error());
    for (const auto& [problem, count] : missing) {
        for (int i = 0; i < count; ++i) {
            insert->reset();
            if (auto done = insert->bind(1, problem.note)
                                .bind(2, problem.rule)
                                .bind(3, problem.level)
                                .bind(4, problem.line)
                                .bind(5, problem.message)
                                .bind(6, problem.subject.isNull() ? QStringLiteral("") : problem.subject)
                                .run();
                !done)
                return std::unexpected(done.error());
            ++report.changed;
        }
    }
    return report;
}

} // namespace coeus
