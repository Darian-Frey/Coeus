#pragma once

#include "index/Database.hpp"

#include <expected>

namespace coeus {

// Derives projects and register membership for the whole index (F-015,
// SPEC.md §Registers). A project is a folder whose README.md opens with the
// status header; a note belongs to the nearest such folder above it in its
// root; a register file's entries belong to its note's project, or to none.
// Citations between entries resolve within a project. Like LinkResolver it
// runs after every change, in the same transaction, and writes only rows
// that differ, keeping existing project ids.
class ProjectResolver {
public:
    struct Report {
        int projects = 0;
        int registerEntries = 0; // entries inside a project
        int changed = 0;         // rows written
    };

    static std::expected<Report, QString> resolveAll(Database& db);
};

} // namespace coeus
