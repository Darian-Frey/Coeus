# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8,
bugs are logged here when found, not silently fixed. The author decides
whether to fix immediately, defer, or leave alone.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

## Open

_None._

## Fixed

### BUG-001: `std::expected` used under C++20
**Status:** fixed
**Severity:** high
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** ARCHITECTURE.md §Cross-cutting concerns, SPEC.md §Sync, DECISIONS.md D-002
**Description.** The error-handling convention and `SyncProvider` rely on `std::expected`, a C++23 feature, while the project was specified as C++20. The first module boundary would not have compiled.
**Fix.** Moved to C++23 (D-013); `tl::expected` fallback removed from ARCHITECTURE.

### BUG-002: Contentless FTS5 table cannot return snippets
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** SPEC.md §Index schema, §MCP surface
**Description.** `fts` was declared with `content=''`. Contentless FTS5 tables cannot serve `snippet()` or `highlight()`, so the search panel and the MCP `search` tool's `snippet` field were unimplementable, and row deletion needs the original values.
**Fix.** `fts` now stores its own copy of the text, keyed by `rowid = notes.id`; the incremental update deletes and re-inserts it explicitly.

### BUG-003: `.gitignore` written without a user command
**Status:** fixed
**Severity:** low
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** SPEC.md §Vault layout, DECISIONS.md D-005, CLAUDE.md §Invariants
**Description.** SPEC said `.coeus/` is added to `.gitignore` on first open, a vault write outside the editor save path and outside any explicit user command.
**Fix.** Coeus now offers the change on first open; accepting is an explicit command through `AtomicWriter`, declining is remembered. The vault layout diagram also gains `ideas/`, `.coeus/journal/` and `.coeus-shared/`.

### BUG-004: `next_id` reservations had no cross-process home
**Status:** fixed
**Severity:** high
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** SPEC.md §ID allocation, §Index schema, §MCP surface
**Description.** The MCP `next_id` tool "reserves for 60 s", but `NextId` was defined only as max+1 over the index, with nowhere to keep reservations visible to both the GUI and `coeus --mcp`. Concurrent allocation could hand out the same ID (AV-003, AV-012).
**Fix.** A `reservations` table in `index.db`; `NextId` runs under `BEGIN IMMEDIATE`, reads existing IDs from the register file on disk, and writes are re-checked against the file immediately before the atomic write.

### BUG-005: Phase 1 scope inconsistent across documents
**Status:** fixed
**Severity:** low
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** CLAUDE.md §Active task, ROADMAP.md Phase 1, FEATURES.md F-001, F-002
**Description.** CLAUDE.md's active task omitted the command palette that F-001 and ROADMAP require, and CLAUDE.md and ROADMAP filtered the explorer to Markdown only while F-002 includes attachment types.
**Fix.** CLAUDE.md and ROADMAP aligned with F-001 and F-002.

## Won't Fix

## Deferred
