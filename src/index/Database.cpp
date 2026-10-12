#include "index/Database.hpp"

#include <QFile>

#include <sqlite3.h>

namespace coeus {

namespace {

constexpr int kBusyTimeoutMs = 5000;

QString lastError(sqlite3* db)
{
    return QString::fromUtf8(sqlite3_errmsg(db));
}

} // namespace

void Database::Closer::operator()(sqlite3* db) const
{
    sqlite3_close_v2(db);
}

Database::Database(sqlite3* db)
    : m_db(db)
{
}

std::expected<Database, QString> Database::open(const QString& path, Mode mode)
{
    sqlite3* raw = nullptr;
    const int flags = (mode == Mode::ReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
                      | SQLITE_OPEN_NOMUTEX;
    const QByteArray encoded = path == QLatin1String(":memory:") ? path.toUtf8() : QFile::encodeName(path);
    const int rc = sqlite3_open_v2(encoded.constData(), &raw, flags, nullptr);
    Database db(raw); // closes even on failure
    if (rc != SQLITE_OK)
        return std::unexpected(QStringLiteral("Cannot open index %1: %2").arg(path, raw ? lastError(raw) : QString()));

    sqlite3_busy_timeout(raw, kBusyTimeoutMs);
    sqlite3_extended_result_codes(raw, 1);
    const char* pragmas = mode == Mode::ReadOnly ? "PRAGMA foreign_keys=ON;"
                                                 : "PRAGMA journal_mode=WAL;"
                                                   "PRAGMA synchronous=NORMAL;"
                                                   "PRAGMA foreign_keys=ON;";
    if (auto result = db.exec(pragmas); !result)
        return std::unexpected(result.error());
    return db;
}

std::expected<void, QString> Database::exec(const char* sql)
{
    char* message = nullptr;
    if (sqlite3_exec(m_db.get(), sql, nullptr, nullptr, &message) != SQLITE_OK) {
        const QString error = QString::fromUtf8(message ? message : "unknown error");
        sqlite3_free(message);
        return std::unexpected(error);
    }
    return {};
}

std::expected<Statement, QString> Database::prepare(const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(m_db.get(), sql, -1, &statement, nullptr) != SQLITE_OK)
        return std::unexpected(QStringLiteral("%1 in: %2").arg(lastError(m_db.get()), QString::fromUtf8(sql)));
    return Statement(m_db.get(), statement);
}

std::int64_t Database::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(m_db.get());
}

int Database::changes() const
{
    return sqlite3_changes(m_db.get());
}

// --- Statement ---------------------------------------------------------------

void Statement::Finalizer::operator()(sqlite3_stmt* statement) const
{
    sqlite3_finalize(statement);
}

Statement::Statement(sqlite3* db, sqlite3_stmt* statement)
    : m_db(db)
    , m_statement(statement)
{
}

Statement& Statement::bind(int index, std::int64_t value)
{
    sqlite3_bind_int64(m_statement.get(), index, value);
    return *this;
}

Statement& Statement::bind(int index, double value)
{
    sqlite3_bind_double(m_statement.get(), index, value);
    return *this;
}

Statement& Statement::bind(int index, const QString& value)
{
    if (value.isNull())
        return bindNull(index);
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(m_statement.get(), index, utf8.constData(), int(utf8.size()), SQLITE_TRANSIENT);
    return *this;
}

Statement& Statement::bind(int index, const QByteArray& value)
{
    sqlite3_bind_blob(m_statement.get(), index, value.constData(), int(value.size()), SQLITE_TRANSIENT);
    return *this;
}

Statement& Statement::bindNull(int index)
{
    sqlite3_bind_null(m_statement.get(), index);
    return *this;
}

std::expected<bool, QString> Statement::step()
{
    switch (sqlite3_step(m_statement.get())) {
    case SQLITE_ROW:
        return true;
    case SQLITE_DONE:
        return false;
    default:
        return std::unexpected(lastError(m_db));
    }
}

std::expected<void, QString> Statement::run()
{
    for (;;) {
        const auto row = step();
        if (!row)
            return std::unexpected(row.error());
        if (!*row)
            return {};
    }
}

void Statement::reset()
{
    sqlite3_reset(m_statement.get());
    sqlite3_clear_bindings(m_statement.get());
}

std::int64_t Statement::int64(int column) const
{
    return sqlite3_column_int64(m_statement.get(), column);
}

double Statement::real(int column) const
{
    return sqlite3_column_double(m_statement.get(), column);
}

QString Statement::text(int column) const
{
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(m_statement.get(), column));
    if (!data)
        return {};
    return QString::fromUtf8(data, sqlite3_column_bytes(m_statement.get(), column));
}

QByteArray Statement::blob(int column) const
{
    const auto* data = static_cast<const char*>(sqlite3_column_blob(m_statement.get(), column));
    return QByteArray(data, sqlite3_column_bytes(m_statement.get(), column));
}

bool Statement::isNull(int column) const
{
    return sqlite3_column_type(m_statement.get(), column) == SQLITE_NULL;
}

int Statement::columnCount() const
{
    return sqlite3_column_count(m_statement.get());
}

// --- Transaction -------------------------------------------------------------

Transaction::Transaction(Database& db)
    : m_db(&db)
{
}

Transaction::Transaction(Transaction&& other) noexcept
    : m_db(other.m_db)
    , m_open(other.m_open)
{
    other.m_open = false;
}

std::expected<Transaction, QString> Transaction::begin(Database& db, Kind kind)
{
    if (auto result = db.exec(kind == Kind::Immediate ? "BEGIN IMMEDIATE" : "BEGIN"); !result)
        return std::unexpected(result.error());
    return Transaction(db);
}

std::expected<void, QString> Transaction::commit()
{
    if (!m_open)
        return std::unexpected(QStringLiteral("Transaction already finished"));
    m_open = false;
    return m_db->exec("COMMIT");
}

Transaction::~Transaction()
{
    if (m_open)
        (void)m_db->exec("ROLLBACK");
}

} // namespace coeus
