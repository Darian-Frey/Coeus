#include "index/Search.hpp"

#include "index/Database.hpp"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QVariant>

#include <sqlite3.h>

#include <memory>

namespace coeus {

namespace {

// regexp(pattern, text): what SQLite's REGEXP operator calls. Compiled
// patterns are cached per connection, since the same one runs on every row.
void regexpFunction(sqlite3_context* context, int, sqlite3_value** argv)
{
    auto* cache = static_cast<QHash<QString, QRegularExpression>*>(sqlite3_user_data(context));
    const auto* pattern = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
    if (!pattern || !text) {
        sqlite3_result_int(context, 0);
        return;
    }
    const QString key = QString::fromUtf8(pattern);
    auto it = cache->find(key);
    if (it == cache->end())
        it = cache->insert(key, QRegularExpression(key, QRegularExpression::MultilineOption));
    sqlite3_result_int(context, it->match(QString::fromUtf8(text)).hasMatch() ? 1 : 0);
}

// SQL LIKE pattern for a literal prefix, with % _ and \ escaped.
QString likePrefix(const QString& text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
        .replace(QLatin1Char('%'), QLatin1String("\\%"))
        .replace(QLatin1Char('_'), QLatin1String("\\_"));
    return escaped + QLatin1Char('%');
}

} // namespace

std::expected<QList<Search::Hit>, QString> Search::run(const QString& indexPath, const SearchQuery& query, int limit)
{
    if (!query.error.isEmpty())
        return std::unexpected(query.error);
    if (query.isEmpty())
        return QList<Hit>();
    if (!QFileInfo::exists(indexPath))
        return std::unexpected(QStringLiteral("The index is not built yet"));
    // Declared first so it outlives the connection that points at it.
    auto cache = std::make_unique<QHash<QString, QRegularExpression>>();
    auto db = Database::open(indexPath, Database::Mode::ReadOnly);
    if (!db)
        return std::unexpected(db.error());

    sqlite3_create_function(db->handle(), "regexp", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC, cache.get(),
                            regexpFunction, nullptr, nullptr);

    // Built from fixed fragments; every value is a bound parameter.
    const QString display = QStringLiteral("(CASE WHEN r.name = 'vault' THEN n.path ELSE r.name || '/' || n.path END)");
    const bool fullText = query.hasFullText();
    QString sql = QStringLiteral("SELECT r.path || '/' || n.path, %1, n.title, %2 FROM notes n "
                                 "JOIN roots r ON r.id = n.root_id JOIN fts ON fts.rowid = n.id WHERE 1")
                      .arg(display, fullText ? QStringLiteral("snippet(fts, 1, char(2), char(3), '…', 16)")
                                             : QStringLiteral("substr(fts.body, 1, 160)"));
    QList<QVariant> values;
    if (fullText) {
        sql += QStringLiteral(" AND fts MATCH ?");
        values.append(query.ftsExpression());
    }
    for (const QString& excluded : query.excluded) {
        sql += QStringLiteral(" AND n.id NOT IN (SELECT rowid FROM fts WHERE fts MATCH ?)");
        values.append(SearchQuery::ftsString(excluded));
    }
    for (const QString& tag : query.tags) {
        sql += QStringLiteral(" AND EXISTS (SELECT 1 FROM tags t WHERE t.note = n.id AND "
                              "(lower(t.tag) = lower(?) OR lower(t.tag) LIKE lower(?) ESCAPE '\\'))");
        values.append(tag);
        values.append(likePrefix(tag + QLatin1Char('/')));
    }
    for (const QString& path : query.paths) {
        sql += QStringLiteral(" AND %1 LIKE ? ESCAPE '\\'").arg(display);
        values.append(likePrefix(path));
    }
    for (const QString& file : query.files) {
        sql += QStringLiteral(" AND instr(lower(n.title), lower(?)) > 0");
        values.append(file);
    }
    for (const QString& pattern : query.patterns) {
        sql += QStringLiteral(" AND fts.body REGEXP ?");
        values.append(pattern);
    }
    // Title matches weigh ten times body matches (bm25 is lower-is-better).
    sql += fullText ? QStringLiteral(" ORDER BY bm25(fts, 10.0, 1.0)") : QStringLiteral(" ORDER BY 2");
    sql += QStringLiteral(" LIMIT ?");
    values.append(limit);

    auto statement = db->prepare(sql.toUtf8().constData());
    if (!statement)
        return std::unexpected(statement.error());
    for (qsizetype i = 0; i < values.size(); ++i) {
        const QVariant& value = values[i];
        if (value.typeId() == QMetaType::Int)
            statement->bind(int(i + 1), value.toInt());
        else
            statement->bind(int(i + 1), value.toString());
    }

    QList<Hit> hits;
    for (;;) {
        const auto row = statement->step();
        if (!row) {
            // A malformed FTS expression is the query's fault, not the index's.
            return std::unexpected(QStringLiteral("Cannot search for that: %1").arg(row.error()));
        }
        if (!*row)
            return hits;
        hits.append({statement->text(0), statement->text(1), statement->text(2),
                     statement->text(3).simplified()});
    }
}

} // namespace coeus
