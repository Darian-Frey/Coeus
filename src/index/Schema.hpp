#pragma once

#include "index/Database.hpp"

namespace coeus {

// The index schema (SPEC.md §Index schema). The index is a cache (D-005), so
// there are no migrations: a database with any other version is deleted and
// rebuilt from the vault.
class Schema {
public:
    static constexpr int kVersion = 5; // 2: registers and projects (F-015); 3: lint (F-016); 4: ideas (F-032); 5: chunks and vectors (F-018)

    // Version stored in `meta`, or 0 for an empty database.
    static std::expected<int, QString> version(Database& db);
    static std::expected<void, QString> create(Database& db);
};

} // namespace coeus
