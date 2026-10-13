# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com).

## [Unreleased]
### Added
- `tools/make_demo_vault.py`: a demo vault with a linked and a missing root, attachments, every link form, tags, properties and a "Start here" checklist; `--bulk N` for performance; `--force` only ever deletes a demo it made. Test `test_make_demo_vault`.
- Properties panel (F-008): typed frontmatter fields above the source editor, with `status`/`stage` as vocabulary drop-downs; invalid values shown as errors and never rewritten; edits, additions and removals change only the key's lines in the buffer (D-015), one undo step each. `Frontmatter::setValue`/`removeKey`/`yamlScalar`/`yamlList`; `registers/Vocabulary`. Tests `test_frontmatter_edit`, `test_properties_panel`.
- Search (F-006): a Search dock (`Ctrl+Shift+F`) with prefix words, phrases, exclusions, `tag:`, `path:`, `file:` and `re:/…/`, BM25 ranking with titles first, marked snippets; hits open with the match selected. Tests `test_search`, `test_search_panel`, and `test_search_speed` (10,000 notes, 29 ms worst case against a 100 ms budget).
- Tag pane (F-007): a Tags dock tabbed with the Explorer listing every tag nested on `/` with note counts, case-insensitive; activating a tag (or a reading-mode tag link) filters the Explorer to its notes until cleared. Tests `test_tags`, `test_tag_panel`.
- Outline panel (F-013): the current note's headings as a tree, parsed from the unsaved buffer off the UI thread; clicking goes to the heading in source or reading mode; the cursor's section is highlighted. Test `test_outline_panel`.
- Backlinks panel (F-005): links to the current note grouped by source with their lines, and unlinked mentions of its title and aliases found by FTS5 and confirmed line by line; clicking opens the source at the line (`MainWindow::openNoteAt`). Tests `test_backlinks`, `test_backlinks_panel`.
- Wikilink completer (F-004): `[[` offers notes and aliases ranked by fuzzy match and nearness, `[[Note#` offers headings; shared titles insert a path so the link resolves to the chosen note (AV-006). `NoteCatalogue` reloads off the UI thread. Tests `test_wikilink_completion`, `test_wikilink_completer`; 10,000 notes rank in 6 ms.
- Reading mode (F-003): Source | Live | Reading switch per tab and `Ctrl+E`; `HtmlRenderer` renders properties, callouts, highlights, tags, task lists, tables and images from md4c; wikilinks open the note the index resolved, Markdown links their relative path. Tests `test_html_renderer`, `test_note_view`.
- Editor (F-003 source mode, F-011): editable tabs with Markdown highlighting; `Ctrl+S` saves through `Vault::writeNote` and `AtomicWriter`; saving over an external change, or an external change to a modified buffer, prompts (Keep Mine / Load Disk / Decide Later) instead of overwriting (AV-002); clean tabs follow external edits within a second; close and quit ask about unsaved changes.
- Autosave journal: modified buffers written to `.coeus/journal/` every 5 s on a background thread; unsaved work offered for recovery when the vault reopens.
- Tests `test_editor`, `test_journal`, `test_conflict_prompt`; sanitizer build documented in BUILD.md.
- First-open `.gitignore` offer (Add / Not Now / Don't Ask Again) and File › Add Coeus Files to .gitignore; `VaultConfig` edits `config.toml` line by line. Tests `test_gitignore_offer` and two window cases.
- Explorer sections for the vault and each linked root, collapsible, missing roots greyed; notes in linked roots open in tabs labelled `<root>/<path>` (F-034 complete).
- Linked roots, ignore rules and live indexing: `Roots` (config.toml via toml++, D-016), `GitIgnore` (libgit2; the repository containing a root and repositories nested in it), `VaultWatcher` (inotify, one watch per folder, D-019), `IndexService` worker thread. Root ids are stable by name; missing roots keep their rows (AV-010). Verify Index and Rebuild Index commands; index status in the status bar. Tests `test_roots`, `test_git_ignore`, `test_vault_watcher`, `test_index_service`. On ~/Projects, 11 nested repositories' rules cut the index from 645 to 272 notes.
- D-019 inotify watcher.
- SQLite index (D-017, D-018): `Database`/`Statement`/`Transaction` wrapper, schema v1, `IndexStore`, `LinkResolver` (SPEC §Wikilink resolution, ambiguity flagged), `Indexer` scan/update/verify, BLAKE2b-128 `ContentHash`. Tests `test_database`, `test_indexer`, `test_rebuild_equivalence` (AV-001). A full scan of 645 notes takes 0.8 s; resolving 1,418 links takes 5 ms.
- Markdown parser (`NoteParser`, `Frontmatter`; D-014, D-015): wikilinks, embeds, local Markdown links, tags, headings, block IDs, typed properties and plain text with file line numbers. `test_note_parser` (25 cases); 305 real notes parse at 0.5 ms each in a debug build.
- `TempSweeper` removes stale `.coeus-tmp-*` files on vault open, off the UI thread (IMP-002 applied); the write invariant names it as the sole exception.
- `tools/check_xrefs.py` cross-reference checker with tests, ctest entries and a pre-commit hook (IMP-001 applied).
- `AtomicWriter` (AV-002): temp write, fsync, rename, directory fsync; keeps mode, owner where permitted, and symlinks. `test_atomic_writer` includes a kill-during-write harness.
- Phase 1 shell (F-001, F-002): CMake project (C++23, `-Wall -Wextra -Werror`), `MainWindow` with explorer and sidebar docks, tool strip, tabs and status bar; vault opened from the command line or File menu; Markdown-and-attachments explorer; read-only note tabs; `Ctrl+P` command palette with fuzzy filter; dock layout persisted through `QSettings`.
- Tests: `test_vault`, `test_fuzzy_match`, `test_main_window`, `test_open_large_vault` (1,000-file tree in 63 ms against a 500 ms budget).
- BUILD.md.
- Initial scaffolding: README, CLAUDE.md, FEATURES (F-001–F-031), ROADMAP (Phases 1–6), ARCHITECTURE, DECISIONS (D-001–D-009), SPEC, ATTACK_VECTORS (AV-001–AV-009), BUGS, IMPROVEMENTS (IMP-001), CHANGELOG.
- D-010 linked project roots, D-011 vault-not-index sync with embeddings sidecar, D-012 MCP server.
- Project directory layout: `src/` module folders (including `src/mcp/`), `tests/`, `tools/`, `docs/design/` with the main-window mockup.
- `LICENSE` (MIT); D-009 Accepted.
- F-032 ideas register and project board, F-033 Coeus MCP server, F-034 linked project roots, F-035 embeddings sidecar.
- AV-010 root path drift, AV-011 sidecar stale vectors, AV-012 register corruption via MCP writes.
- SPEC: §Linked roots, §Ideas, §Embeddings sidecar, §MCP surface; `roots` and `ideas` tables; `root_id` on `notes` and `projects`; root-aware wikilink resolution.
- D-013 C++23 language standard.
- D-014 to D-018 (Accepted): md4c, yaml-cpp, toml++, libsodium, SQLite C API for Phase 2.
- `reservations` table and cross-process `NextId` allocation (SPEC.md §ID allocation).
### Changed
- The sidebar's Properties placeholder is gone: properties sit above the note, as F-008 and the mockup place them.
- D-014 amended: reading mode uses `HtmlRenderer`, not md4c-html, which is no longer a dependency. The attachment-extension rule moves into `parser/` so the parser does not depend on `index/`.
- SPEC §Autosave journal: journal entries older than their note are offered too, flagged, rather than hidden; new §Editing.
- libgit2 is now a build dependency (for `.gitignore` rules), ahead of Phase 3's Git features.
- SPEC §Linked roots gains name, path, id, absent-root, removed-root and ignore rules; new §File watching.
- SPEC §Index schema: `links.ambiguous`; no migrations, a mismatched or unreadable index is rebuilt; indexed-file rules; resolution recomputed per update; file links (attachments, Markdown links to non-`.md` files) are neither resolved nor counted as broken.
- SPEC §Markdown dialect: local Markdown links are links, resolved relative to the note's folder; all-digit tags excluded and tag letters may be any script, as in Obsidian; embeds found by `parser/`; frontmatter property type inference. `links` gains `style`, `props` gains `line`.
- ROADMAP Phase 2 deliverables now cover every Phase 2 feature's acceptance: `AtomicWriter`, rebuild-equivalence check, `.gitignore` offer, autosave journal, conflict prompt, reading mode, unlinked mentions.
- Phase 1 (GUI shell) Complete; F-001 and F-002 Complete (2026-10-12).
- Minimum Qt lowered from 6.5 to 6.4 to build against the distribution's packaged Qt (Mint 22, Ubuntu 24.04).
- C++20 → C++23 so `std::expected` is available (BUG-001, D-013).
- `fts` stores its own text so snippets work (BUG-002).
- `.coeus/` in `.gitignore` is offered on first open, not written unasked (BUG-003).
- CLAUDE.md and ROADMAP Phase 1 aligned with F-001/F-002: command palette, attachments in the explorer (BUG-005).
- SPEC §Vault layout shows `ideas/`, `.coeus/journal/`, `.coeus-shared/`.
- F-017 review-freshness dashboard withdrawn, absorbed into F-032.
- ROADMAP Phase 2 gains F-034; Phase 3 gains F-032, F-033, F-035; Phase 5 acceptance covers two-machine convergence.
