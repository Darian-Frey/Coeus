# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8,
bugs are logged here when found, not silently fixed. The author decides
whether to fix immediately, defer, or leave alone.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

## Open

### BUG-010: A status message is drawn over the note's path
**Status:** open
**Severity:** low
**Found:** 2026-10-15 (screenshots of F-027's themes on the demo vault, in every theme)
**Location:** `src/ui/MainWindow.cpp` (`createStatusBar`, `m_noteLabel`)
**Description.** With a note open in a vault that has a missing linked root, the bottom-left of the status bar shows the "Linked root … is missing" message and the note's path (`projects/coeus/DECISIONS.md`) on top of each other, both unreadable. `m_noteLabel` is a normal (not permanent) status-bar widget, which `QStatusBar` hides while a message shows, so something shows it again or sets its text in a way that keeps it visible; not yet investigated. Seen in System, Dark and Cassette alike, so it predates F-027.
**Fix.** Not yet fixed.

## Fixed

### BUG-008: The embeddings sidecar counted as a change in Git vaults
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-15 (F-036 and F-037, `test_commit_dialog` failing on a machine with the model)
**Fixed:** 2026-10-15
**Location:** `src/git/GitRepository.cpp` (`isCoeusFile`)
**Description.** In a vault that is a Git repository, the embedding worker's `.coeus-shared/embeddings.bin` (F-035) was an untracked file like any other: the status bar's changed-files count went up after the first embedding pass and again after every edit was embedded, and the commit dialog listed it each time. Introduced with F-035 (773ef39); its tests passed because `test_commit_dialog` usually finished before the worker wrote the sidecar, and the suite runs without the model elsewhere.
**Fix.** `.coeus-shared/` joins `.coeus/` as Coeus's own: never counted, listed or committed by Coeus. It still travels by the sync provider; committing it by hand is the author's choice. `test_git_commit` gains the sidecar in status and as a refused commit; both fail with the old code.

### BUG-007: A root whose own README is a project could not be indexed
**Status:** fixed
**Severity:** high
**Found:** 2026-10-13 (F-016, linting Coeus's own registers in a test)
**Fixed:** 2026-10-13
**Location:** `src/index/ProjectResolver.cpp` (`folderOf`), `src/registers/NextId.cpp`
**Description.** A project at a root's top level has the folder "" — a linked repository whose README carries the status header (F-034), or a vault that is itself one. `folderOf` returned a null `QString` there, which binds as SQL NULL, so the project insert broke `projects.path NOT NULL` and the root's whole scan failed: none of its notes were indexed. The demo vault's Pontus root was one. Introduced with F-015 step 1 (fdfb1b3); the screenshot check then opened the demo's parent folder, where Pontus was a subfolder, so it was missed. `NextId` would likewise have refused such a project, given the null path a query returns for "".
**Fix.** `folderOf` returns `""` (in `ProjectResolver` and `LintResolver`), and `NextId` binds a null project path as `""`. Tested by `test_lint` (Coeus's own README makes its root a project) and `test_next_id::aRootThatIsItselfAProject`; both fail with the old code.

### BUG-006: Field names with a hyphen were not parsed
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-13 (F-016, surveying real registers before writing L6)
**Fixed:** 2026-10-13
**Location:** `src/parser/RegisterParser.cpp`, SPEC.md §Entry grammar
**Description.** The field pattern `[A-Za-z ]+` from SPEC excluded the hyphen, so the standard's own `**Trade-offs:**` was never a field: it was dropped, or swallowed by the field before it. Across `~/Projects`, 66 of 68 IMP entries carry it; L6 would have flagged nearly all of them.
**Fix.** Names are `[A-Za-z][A-Za-z -]*` in the parser and SPEC. `test_register_parser` parses a `Trade-offs` field; `test_lint` lints Coeus's own registers and requires no errors (AV-008).

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
**Fix.** Coeus now offers the change on first open; accepting is an explicit command through `AtomicWriter`, declining is remembered. The vault layout diagram also gains `ideas/`, `.coeus/journal/` and `.coeus-shared/`. IMP-002 later added `.*.coeus-tmp-*` to the same offer.

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
