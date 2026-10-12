# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com).

## [Unreleased]
### Added
- `AtomicWriter` (AV-002): temp write, fsync, rename, directory fsync; keeps mode, owner where permitted, and symlinks. `test_atomic_writer` includes a kill-during-write harness.
- IMP-002 sweep of stale `.coeus-tmp-*` files.
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
