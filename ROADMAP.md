# Roadmap

Phases are append-only. Mark Complete with an ISO 8601 date.

## Phase 1 — GUI shell
**Goal:** A running Qt6 window with the full panel skeleton so every later feature can be tested the moment it is wired in.
**Status:** Not started
**Features delivered:** F-001, F-002
**Deliverables:**
- [ ] CMake project with `src/app`, `src/ui` and a test target (`CMakeLists.txt`)
- [ ] `MainWindow` with central editor placeholder, explorer dock, sidebar dock, toolbar, status bar, tab widget (`src/ui/MainWindow.cpp`)
- [ ] Vault open from command line; `QFileSystemModel` tree filtered to Markdown and attachment types (`src/vault/Vault.cpp`, `src/ui/ExplorerPanel.cpp`)
- [ ] Read-only load of a selected file into `QPlainTextEdit`
- [ ] Command palette dialog with fuzzy action filter (`src/ui/CommandPalette.cpp`)
- [ ] Layout persistence through `QSettings`
- [ ] `BUILD.md` written from the first successful build
**Acceptance:** Builds with `-Wall -Wextra -Werror`; opens a 1,000-file vault and shows the tree in under 500 ms; docks can be dragged, floated, hidden and restored.

## Phase 2 — Core vault
**Goal:** Everything needed to use Coeus daily instead of Obsidian for plain notes.
**Status:** Not started
**Features delivered:** F-003 (source and reading modes), F-004, F-005, F-006, F-007, F-008, F-009, F-010, F-011, F-013, F-034
**Deliverables:**
- [ ] Markdown parser with wikilink, tag, frontmatter extensions (`src/parser/`)
- [ ] SQLite index: roots, notes, links, tags, properties, FTS5 (`src/index/`)
- [ ] Linked project roots from `config.toml`, one watcher per root (`src/vault/Roots.cpp`)
- [ ] Incremental indexer driven by the file watchers (`src/vault/VaultWatcher.cpp`, `src/index/Indexer.cpp`)
- [ ] Editor with syntax highlighting and atomic save (`src/ui/Editor.cpp`)
- [ ] Wikilink completer, backlinks panel, outline panel, tag pane, search panel
- [ ] Properties panel with typed validation
- [ ] Daily notes and templates
**Acceptance:** A real vault (the author's project repos) can be browsed, searched, edited and saved for a week without data loss or index drift (AV-001, AV-002).

## Phase 3 — Differentiators
**Goal:** The three features that make Coeus more than an Obsidian clone.
**Status:** Not started
**Features delivered:** F-015, F-016, F-018, F-019, F-020, F-021, F-022, F-032, F-033, F-035
**Deliverables:**
- [ ] Register parser and model over the scaffold document types (`src/registers/`)
- [ ] Register panel, ID completion, "Insert next ID" command, Problems panel
- [ ] Ideas register and project board with freshness, phase and Git ahead/behind columns (`src/ui/ProjectBoard.cpp`)
- [ ] Mnemosyne embedding worker and `sqlite-vec` table (`src/search/Semantic.cpp`)
- [ ] Embeddings sidecar export, import and compaction (`src/search/Sidecar.cpp`)
- [ ] Semantic query mode and related-notes sidebar
- [ ] libgit2 wrapper: history, diff, blame, commit, fetch, ahead/behind (`src/git/`)
- [ ] Git sidebar, inline diff view, gutter blame, commit dialog
- [ ] MCP server with read tools, then opt-in write tools with GUI confirmation (`src/mcp/`)
**Acceptance:** Opening the vault plus linked repos shows every register with correct counts; a semantic query for a decision by meaning finds it; a note's history, blame and diff match `git log`, `git blame`, `git diff` output; Claude Code in Antigravity can log a DECISIONS entry through MCP that appears in the register panel with the correct next ID.

## Phase 4 — Rich Markdown and workspace
**Goal:** Feature parity with the parts of Obsidian's core that Phase 2 deferred.
**Status:** Not started
**Features delivered:** F-003 (live preview), F-012, F-014, F-023, F-024, F-025, F-026, F-027
**Deliverables:**
- [ ] Live-preview editing mode
- [ ] Splits and native multi-window
- [ ] Embeds, block references, callouts, Mermaid, maths, code highlighting
- [ ] Graph view
- [ ] Themes and rebindable hotkeys
**Acceptance:** A note using every rich construct renders identically in live preview and reading mode; two windows on two monitors keep independent dock layouts.

## Phase 5 — Sync
**Goal:** Vault available on a second Linux machine with conflict safety.
**Status:** Not started
**Features delivered:** F-028, F-029
**Deliverables:**
- [ ] `SyncProvider` interface and rclone implementation over root 0 only, `.coeus/` excluded, `.coeus-shared/` included (`src/sync/`)
- [ ] Native Google Drive provider with OAuth, change log, three-way merge, optional encryption
**Acceptance:** Desktop and laptop converge on the same vault after a sync with each rebuilding its own index; the laptop imports the desktop's embeddings from the sidecar and embeds only changed chunks; concurrent edits to the same note on two machines produce a conflict prompt, never silent loss (AV-004).

## Phase 6 — Canvas and views
**Goal:** The remaining Obsidian surface area.
**Status:** Not started
**Features delivered:** F-030, F-031
**Deliverables:**
- [ ] Canvas editor and JSON format
- [ ] Table and card views over properties
**Acceptance:** Both features round-trip through the vault as plain files that Git can diff.
