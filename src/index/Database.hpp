#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <expected>
#include <memory>

struct sqlite3;
struct sqlite3_stmt;

namespace coeus {

class Statement;

// Thin RAII wrapper over the SQLite C API (D-018). One Database per thread
// (ARCHITECTURE.md §Threading). Errors are returned, never thrown.
class Database {
public:
    enum class Mode { ReadWrite, ReadOnly };

    // Opens or creates `path` (":memory:" for a private in-memory database),
    // with WAL, foreign keys and a busy timeout.
    static std::expected<Database, QString> open(const QString& path, Mode mode = Mode::ReadWrite);

    // Runs one or more statements without results.
    std::expected<void, QString> exec(const char* sql);
    std::expected<Statement, QString> prepare(const char* sql);

    std::int64_t lastInsertRowId() const;
    int changes() const;
    sqlite3* handle() const { return m_db.get(); }

private:
    struct Closer {
        void operator()(sqlite3* db) const;
    };

    explicit Database(sqlite3* db);
    std::unique_ptr<sqlite3, Closer> m_db;
};

class Statement {
public:
    // Parameters are 1-based, as in SQLite.
    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, std::int64_t(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, const QString& value); // null QString binds NULL
    Statement& bind(int index, const QByteArray& value); // as a blob
    Statement& bindNull(int index);

    // Advances; true while a row is available.
    std::expected<bool, QString> step();
    // Steps to completion, for statements without results.
    std::expected<void, QString> run();
    void reset();

    std::int64_t int64(int column) const;
    double real(int column) const;
    QString text(int column) const;
    QByteArray blob(int column) const;
    bool isNull(int column) const;
    int columnCount() const;

private:
    friend class Database;
    struct Finalizer {
        void operator()(sqlite3_stmt* statement) const;
    };

    Statement(sqlite3* db, sqlite3_stmt* statement);
    sqlite3* m_db;
    std::unique_ptr<sqlite3_stmt, Finalizer> m_statement;
};

// Rolls back on destruction unless committed.
class Transaction {
public:
    enum class Kind { Deferred, Immediate };

    static std::expected<Transaction, QString> begin(Database& db, Kind kind = Kind::Deferred);
    std::expected<void, QString> commit();

    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&&) = delete;
    Transaction(const Transaction&) = delete;
    ~Transaction();

private:
    explicit Transaction(Database& db);
    Database* m_db;
    bool m_open = true;
};

} // namespace coeus
