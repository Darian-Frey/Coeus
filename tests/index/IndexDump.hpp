#pragma once

#include "index/Database.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

// Test helpers for the index suites.
namespace coeus::testing {

// Every derived row in the index, with note ids replaced by "root:path" so
// two indexes built in different orders compare equal when their content is.
inline QStringList dumpIndex(Database& db)
{
    static const char* const kQueries[] = {
        "SELECT 'root', id, name, path, present FROM roots",
        "SELECT 'note', r.name, n.path, n.title, n.mtime, n.size, hex(n.hash) "
        "FROM notes n JOIN roots r ON r.id = n.root_id",
        "SELECT 'link', s.root_id || ':' || s.path, l.target_raw, l.style, l.heading, l.block, l.alias, l.line, "
        "l.embed, l.ambiguous, t.root_id || ':' || t.path "
        "FROM links l JOIN notes s ON s.id = l.src LEFT JOIN notes t ON t.id = l.target_note",
        "SELECT 'tag', n.root_id || ':' || n.path, t.tag, t.line FROM tags t JOIN notes n ON n.id = t.note",
        "SELECT 'prop', n.root_id || ':' || n.path, p.key, p.value, p.type, p.line FROM props p JOIN notes n ON n.id = p.note",
        "SELECT 'heading', n.root_id || ':' || n.path, h.level, h.text, h.line FROM headings h JOIN notes n ON n.id = h.note",
        "SELECT 'block', n.root_id || ':' || n.path, b.block_id, b.line FROM blocks b JOIN notes n ON n.id = b.note",
        "SELECT 'fts', n.root_id || ':' || n.path, f.title, f.body, f.path FROM fts f JOIN notes n ON n.id = f.rowid",
        "SELECT 'fts-orphans', count(*) FROM fts WHERE rowid NOT IN (SELECT id FROM notes)",
        "SELECT 'project', root_id || ':' || path, status, provenance, last_reviewed, why FROM projects",
        "SELECT 'note-project', n.root_id || ':' || n.path, p.root_id || ':' || p.path "
        "FROM notes n LEFT JOIN projects p ON p.id = n.project_id",
        "SELECT 'register', n.root_id || ':' || n.path, r.ident, r.title, r.status, r.line, p.path "
        "FROM registers r JOIN notes n ON n.id = r.note LEFT JOIN projects p ON p.id = r.project_id",
        "SELECT 'field', n.root_id || ':' || n.path, r.ident, f.name, f.value, f.line FROM register_fields f "
        "JOIN registers r ON r.id = f.reg JOIN notes n ON n.id = r.note",
        "SELECT 'ref', n.root_id || ':' || n.path, r.ident, x.target_ident, x.line, tn.root_id || ':' || tn.path, t.line "
        "FROM register_refs x JOIN registers r ON r.id = x.src JOIN notes n ON n.id = r.note "
        "LEFT JOIN registers t ON t.id = x.target LEFT JOIN notes tn ON tn.id = t.note",
        "SELECT 'chunk', n.root_id || ':' || n.path, c.ordinal, c.heading, c.start_line, c.end_line, hex(c.hash) FROM chunks c "
        "JOIN notes n ON n.id = c.note",
        "SELECT 'idea', n.root_id || ':' || n.path, i.stage, i.codename, i.repo, i.summary FROM ideas i "
        "JOIN notes n ON n.id = i.note",
        "SELECT 'mention', n.root_id || ':' || n.path, r.ident, m.ident, m.line FROM register_mentions m "
        "JOIN registers r ON r.id = m.src JOIN notes n ON n.id = r.note",
        "SELECT 'lint', n.root_id || ':' || n.path, l.rule, l.level, l.line, l.message, l.subject FROM lint l "
        "JOIN notes n ON n.id = l.note",
    };

    QStringList rows;
    for (const char* sql : kQueries) {
        auto statement = db.prepare(sql);
        if (!statement)
            return {QStringLiteral("ERROR %1").arg(statement.error())};
        const int columns = statement->columnCount();
        while (statement->step().value_or(false)) {
            QStringList fields;
            for (int c = 0; c < columns; ++c)
                fields.append(statement->isNull(c) ? QStringLiteral("∅") : statement->text(c));
            rows.append(fields.join(QLatin1Char('|')));
        }
    }
    rows.sort();
    return rows;
}

inline void writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(content);
}

} // namespace coeus::testing
