# Attack Vectors

Project-specific failure modes the project must be resilient against.
Grouped by category. Each vector lists detection method and severity.
Severity: Critical (must hold) | Major (regression on release blocks) | Minor (track only).

## Data integrity

### AV-001 Index drift from vault
**Severity:** Critical
**Description.** The SQLite index disagrees with the files on disk — a note exists but is not indexed, a deleted note still appears, or backlinks point at stale content. Causes: missed watcher events, inotify watch exhaustion, crashes mid-transaction, rename-based saves seen as delete only.
**Detection.** not implemented (would require `tests/index/test_rebuild_equivalence.cpp`: index a fixture vault incrementally through a scripted sequence of edits, then rebuild from scratch and assert the two databases are row-identical; plus a "Verify index" command in the app comparing `notes.hash` to disk).
**Related decisions.** D-005 (vault is truth, index is cache).
**History.** Identified during scaffolding 2026-09-25.

### AV-002 Data loss on save or external edit
**Severity:** Critical
**Description.** A note is truncated, half-written, or overwritten with stale editor contents. Causes: non-atomic write interrupted by crash; editor saving over a file changed on disk since it was loaded; sync provider replacing a file with unsaved editor changes.
**Detection.** not implemented (would require `tests/vault/test_atomic_writer.cpp` with a kill-during-write harness, and `tests/ui/test_conflict_prompt.cpp` asserting that a modified-on-disk note with unsaved changes prompts rather than saves).
**Related decisions.** D-005.
**History.** Identified during scaffolding 2026-09-25.

### AV-003 Register ID collision or renumbering
**Severity:** Critical
**Description.** Two entries in one project share an ID, or an existing ID changes number, breaking every outside reference (commits, other projects, papers). Causes: "Insert next ID" racing with an external edit; two machines allocating the same ID before sync; a well-meaning refactor.
**Detection.** not implemented (would require `tests/registers/test_next_id.cpp` and a lint rule flagging duplicate `(project, kind, number)` — the index `UNIQUE` constraint catches it at insert time but must surface as a Problems-panel error, not a silent skip).
**Related decisions.** D-006.
**History.** Identified during scaffolding 2026-09-25. Cross-process reservation and write-time re-check specified in SPEC.md §ID allocation, 2026-10-12 (BUG-004).

### AV-004 Sync conflict clobbers a version
**Severity:** Critical
**Description.** Two machines edit the same note; sync keeps one and silently discards the other. Also covers the rclone `.conflict-*` file being ignored, and the native Drive provider mis-identifying the common ancestor.
**Detection.** not implemented (would require `tests/sync/test_conflict.cpp` driving two vault fixtures through a mock provider and asserting a `Conflict` is raised, never a silent overwrite).
**Related decisions.** D-007.
**History.** Identified during scaffolding 2026-09-25.

## Performance

### AV-005 UI thread blocked by background work
**Severity:** Major
**Description.** Indexing a large vault, embedding, `git blame` on a long history, or a sync pass freezes the window. Also covers SQLite write locks held by a worker while the UI thread tries to read.
**Detection.** not implemented (would require a frame-time watchdog in debug builds logging any main-thread stall over 100 ms, and `tests/perf/test_open_large_vault.cpp` asserting the 500 ms tree target from F-002 on a 1,000-file fixture and 10,000-file stress fixture).
**Related decisions.** D-002, D-004.
**History.** Identified during scaffolding 2026-09-25.

## Correctness

### AV-006 Wikilink resolution ambiguity
**Severity:** Major
**Description.** Two notes share a title in different folders (every project has a `README.md`, `DECISIONS.md`, and so on); a `[[README]]` link resolves to the wrong project's file, or resolution changes when a new note is added elsewhere in the vault.
**Detection.** not implemented (would require `tests/parser/test_link_resolution.cpp` covering each rule in SPEC.md §Wikilink resolution and asserting ambiguous cases are flagged rather than guessed).
**Related decisions.** D-005, D-010.
**History.** Identified during scaffolding 2026-09-25. Especially likely in this vault because the scaffold standard mandates identical filenames across projects. Linked roots (D-010) add a root dimension to resolution, 2026-10-12.

### AV-007 Mixed embedding spaces
**Severity:** Major
**Description.** Vectors from two different models or dimensions end up in the same `vec` table after a model change, making similarity scores meaningless without any error.
**Detection.** not implemented (would require a startup check comparing `meta.embed_model`/`embed_dim` to the live model and a test asserting a mismatch drops and rebuilds `vec`).
**Related decisions.** D-008, D-011.
**History.** Identified during scaffolding 2026-09-25. The embeddings sidecar (D-011) is a second entry point for mixed vectors, 2026-10-12.

### AV-008 Register parser drift from the documentation standard
**Severity:** Major
**Description.** The standard evolves (new document types, field names, vocabularies) and Coeus silently stops recognising entries, or misclassifies fields, without reporting anything.
**Detection.** not implemented (would require a fixture set of every skeleton from `development_documentation.md` under `tests/registers/fixtures/`, re-run whenever the standard's Last reviewed date changes; plus a lint counter that warns when a register file parses to zero entries).
**Related decisions.** D-006.
**History.** Identified during scaffolding 2026-09-25.

### AV-010 Linked root path drift between machines
**Severity:** Major
**Description.** A root exists on the desktop at one path and on the laptop at another, or not at all. Notes vanish from search, cross-root links break, or the indexer deletes rows for a root that is merely absent today.
**Detection.** not implemented (would require `tests/vault/test_missing_root.cpp` asserting that an absent root sets `roots.present = 0` and removes nothing, and that re-appearing restores without re-index when hashes match).
**Related decisions.** D-010.
**History.** Identified 2026-10-12 while adding linked roots.

### AV-011 Sidecar imports a stale or foreign vector
**Severity:** Major
**Description.** `embeddings.bin` carries a vector whose chunk hash matches but which came from a different model build, or a `.conflict-*` copy from Drive is silently dropped, losing the laptop's embeddings.
**Detection.** not implemented (would require `tests/search/test_sidecar.cpp` covering model mismatch rejection, union import of conflict copies, and compaction correctness).
**Related decisions.** D-011.
**History.** Identified 2026-10-12.

## Security

### AV-012 Register corruption through MCP writes
**Severity:** Critical
**Description.** An AI session calls `append_entry` or `write_note` and produces a duplicate ID, writes outside the vault, overwrites a note with unsaved editor changes, or inserts a vocabulary value the lint would reject. The same invariants that bind the UI must bind the MCP path.
**Detection.** not implemented (would require `tests/mcp/test_write_tools.cpp`: concurrent `next_id` reservations never collide; `write_note` with `..` or absolute paths is refused; a write to a note open with unsaved changes is refused; invalid vocabulary is refused before the file is touched).
**Related decisions.** D-012.
**History.** Identified 2026-10-12.

### AV-009 Plaintext leak through the sync provider
**Severity:** Major
**Description.** With encryption enabled, a code path (journal files, conflict copies, log output, temp files) reaches the provider unencrypted, or filenames leak note titles.
**Detection.** not implemented (would require a mock provider in `tests/sync/` that asserts every byte it receives fails a plaintext heuristic when encryption is on, and that `.coeus/` is excluded from sync).
**Related decisions.** D-007.
**History.** Identified during scaffolding 2026-09-25.
