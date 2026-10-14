# Attack Vectors

Project-specific failure modes the project must be resilient against.
Grouped by category. Each vector lists detection method and severity.
Severity: Critical (must hold) | Major (regression on release blocks) | Minor (track only).

## Data integrity

### AV-001 Index drift from vault
**Severity:** Critical
**Description.** The SQLite index disagrees with the files on disk — a note exists but is not indexed, a deleted note still appears, or backlinks point at stale content. Causes: missed watcher events, inotify watch exhaustion, crashes mid-transaction, rename-based saves seen as delete only.
**Detection.** Partly implemented. `tests/index/test_rebuild_equivalence.cpp` indexes a fixture vault with a linked root, applies 8 scripted edits (create, delete, rename, touch, duplicate title, alias, linked-root edit, invalid frontmatter) and 150 seeded random ones through `Indexer::update`, and asserts at 15 checkpoints that the index is row-identical to a fresh rebuild. Disabling link re-resolution in `update` makes it fail at the first checkpoint. `Indexer::verify` compares `notes.hash` to disk; not implemented: the "Verify index" command that exposes it in the app.
**Related decisions.** D-005 (vault is truth, index is cache).
**History.** Identified during scaffolding 2026-09-25.

### AV-002 Data loss on save or external edit
**Severity:** Critical
**Description.** A note is truncated, half-written, or overwritten with stale editor contents. Causes: non-atomic write interrupted by crash; editor saving over a file changed on disk since it was loaded; sync provider replacing a file with unsaved editor changes.
**Detection.** Implemented. `tests/vault/test_atomic_writer.cpp` kills a helper process mid-write 40 times on a 4 MB file and asserts the target is always wholly the old or wholly the new content. `tests/ui/test_editor.cpp` asserts a save over an external change is refused until resolved. `tests/ui/test_conflict_prompt.cpp` drives the app end to end: an external edit to a note with unsaved changes prompts and clobbers nothing, a save over an external change prompts, unsaved work survives a simulated crash through the journal, and closing a modified tab asks. `tests/vault/test_journal.cpp` covers the journal format and recovery rules. Not yet covered: the sync provider path (Phase 5).
**Related decisions.** D-005.
**History.** Identified during scaffolding 2026-09-25.

### AV-003 Register ID collision or renumbering
**Severity:** Critical
**Description.** Two entries in one project share an ID, or an existing ID changes number, breaking every outside reference (commits, other projects, papers). Causes: "Insert next ID" racing with an external edit; two machines allocating the same ID before sync; a well-meaning refactor.
**Detection.** Partly implemented. The index has no `UNIQUE(project, kind, number)` constraint, so a duplicate is indexed rather than lost in a failed insert, and citations to a duplicated ident resolve deterministically (`tests/index/test_rebuild_equivalence.cpp` duplicates idents within and across files; resolving by row id makes it fail). Allocation is covered by `tests/index/test_next_id.cpp`: eight threads allocating at once get distinct consecutive IDs (a deferred transaction makes it fail), and IDs on disk but not yet indexed, in an unsaved buffer, or reserved are never reissued; `tests/ui/test_register_commands.cpp` checks the window passes its unsaved buffer once the reservation has lapsed. Duplicates are lint L9, an error in the Problems panel and underlined at the later heading (F-016; `tests/index/test_lint.cpp`).
**Related decisions.** D-006.
**History.** Identified during scaffolding 2026-09-25. Cross-process reservation and write-time re-check specified in SPEC.md §ID allocation, 2026-10-12 (BUG-004). `NextId` built with its concurrency test, 2026-10-13.

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
**History.** Identified during scaffolding 2026-09-25. Since 2026-10-13 every libgit2 call (history, versions, diff, blame) runs on a one-thread Git worker; blame of a 175-line note with 69 commits takes about 40 ms there and is repeated only after typing pauses. Status and commits run there too; push is a background `git` process (D-020), stopped after 120 s. Embedding (D-008) is blocking llama.cpp work, about 40 ms a passage, so it must only ever run on a worker thread.

## Correctness

### AV-006 Wikilink resolution ambiguity
**Severity:** Major
**Description.** Two notes share a title in different folders (every project has a `README.md`, `DECISIONS.md`, and so on); a `[[README]]` link resolves to the wrong project's file, or resolution changes when a new note is added elsewhere in the vault.
**Detection.** Implemented in `tests/index/test_indexer.cpp` (`wikilinkResolutionOrder`, `markdownLinkResolution`): each rule in SPEC.md §Wikilink resolution, including same-named READMEs in sibling projects, and an equal-distance case that is flagged `ambiguous` rather than guessed. Prevention: the `[[` completer inserts a path whenever a title is shared, so links it writes resolve to the chosen note (`tests/ui/test_wikilink_completion.cpp`, `tests/ui/test_wikilink_completer.cpp`).
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
**Detection.** Partly implemented. `tests/parser/test_register_parser.cpp` parses Coeus's own DECISIONS.md and FEATURES.md and requires every `### D-` heading to become an entry with a status. `tests/index/test_lint.cpp` indexes Coeus's README and six registers and requires the lint to report no errors, so a field the parser stops recognising, or a vocabulary it gets wrong, fails it (it caught BUG-006 and BUG-007). Not implemented: the full skeleton fixture set from `development_documentation.md`, and the zero-entries warning.
**Related decisions.** D-006.
**History.** Identified during scaffolding 2026-09-25. Happened 2026-10-13: the field pattern dropped the standard's `Trade-offs` (BUG-006), found by surveying real registers before writing the lint.

### AV-010 Linked root path drift between machines
**Severity:** Major
**Description.** A root exists on the desktop at one path and on the laptop at another, or not at all. Notes vanish from search, cross-root links break, or the indexer deletes rows for a root that is merely absent today.
**Detection.** Implemented in `tests/index/test_index_service.cpp` (`missingRootKeepsItsNotes`): a root indexed on one path, reopened with a missing path, keeps every row with `roots.present = 0`, and is only emptied when removed from config. Re-appearing roots are rescanned, and unchanged hashes skip re-parsing.
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
**Detection.** Implemented. `NextId`, shared by the window and the MCP write tools, serialises allocation with `BEGIN IMMEDIATE` across connections (`tests/index/test_next_id.cpp`, eight concurrent allocators). `tests/mcp/test_mcp_server.cpp`: writes refused until `allow_writes`; `append_entry` refusing a missing required field, a value outside its vocabulary and a skipped ident without reserving anything, and creating a missing register; `write_note` refusing register files, hidden paths and `..`. `tests/index/test_entry_writer.cpp`: what `append_entry` writes parses back as meant and passes the lint. `tests/ui/test_mcp_window.cpp`, with the real `coeus --mcp` and a window: writes to a note or register open with unsaved changes are refused, `next_id` counts the unsaved IDs, and Undo restores a write or removes a created file but never touches a file changed since. Removing either the unsaved check or Undo's check fails those tests.
**Related decisions.** D-012, D-021.
**History.** Identified 2026-10-12. Write tools built with these guards 2026-10-14.

### AV-009 Plaintext leak through the sync provider
**Severity:** Major
**Description.** With encryption enabled, a code path (journal files, conflict copies, log output, temp files) reaches the provider unencrypted, or filenames leak note titles.
**Detection.** not implemented (would require a mock provider in `tests/sync/` that asserts every byte it receives fails a plaintext heuristic when encryption is on, and that `.coeus/` is excluded from sync).
**Related decisions.** D-007.
**History.** Identified during scaffolding 2026-09-25.
