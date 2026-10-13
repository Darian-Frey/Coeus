#pragma once

#include "index/Database.hpp"

#include <expected>

namespace coeus {

// Runs the register lint (F-016) over the whole index and stores its
// problems in `lint`. Like ProjectResolver, which it follows, it runs after
// every change in the same transaction and writes only rows that differ.
// L8 (stale review) depends on the date and is not stored; IndexQueries
// evaluates it when problems are read.
class LintResolver {
public:
    struct Report {
        int problems = 0;
        int changed = 0; // rows written or deleted
    };

    static std::expected<Report, QString> resolveAll(Database& db);
};

} // namespace coeus
