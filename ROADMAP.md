# Roadmap

Phases are append-only. Mark Complete with an ISO 8601 date.

## Phase 1 — GUI shell
**Goal:** A running Qt6 window with the full panel skeleton so every later feature can be tested the moment it is wired in.
**Status:** Complete (2026-10-12)
**Features delivered:** F-001, F-002
**Deliverables:**
- [x] CMake project with `src/app`, `src/ui` and a test target (`CMakeLists.txt`)
- [x] `MainWindow` with central editor placeholder, explorer dock, sidebar dock, toolbar, status bar, tab widget (`src/ui/MainWindow.cpp`)
- [x] Vault open from command line; `QFileSystemModel` tree filtered to Markdown and attachment types (`src/vault/Vault.cpp`, `src/ui/ExplorerPanel.cpp`)
- [x] Read-only load of a selected file into `QPlainTextEdit`
- [x] Command palette dialog with fuzzy action filter (`src/ui/CommandPalette.cpp`)
- [x] Layout persistence through `QSettings`
- [x] `BUILD.md` written from the first successful build
**Acceptance:** Builds with `-Wall -Wextra -Werror`; opens a 1,000-file vault and shows the tree in under 500 ms; docks can be dragged, floated, hidden and restored.

## Phase 2 — Core vault
**Goal:** Everything needed to use Coeus daily instead of Obsidian for plain notes.
**Status:** In progress: every deliverable is built (2026-10-13); the acceptance week of daily use is the author's to run
**Features delivered:** F-003 (source and reading modes), F-004, F-005, F-006, F-007, F-008, F-009, F-010, F-011, F-013, F-034
**Deliverables:**
- [x] `AtomicWriter`: temp write, fsync, rename, directory fsync, mode and ownership preserved, with a kill-during-write test (`src/vault/AtomicWriter.cpp`, `tests/vault/test_atomic_writer.cpp`; SPEC.md §Atomic write, AV-002)
- [x] Markdown parser with wikilink, tag, frontmatter, heading and block-ID extensions (`src/parser/`)
- [x] SQLite index: roots, notes, links, tags, properties, headings, blocks, FTS5 (`src/index/`)
- [x] Rebuild-equivalence test and a "Verify index" command comparing `notes.hash` to disk (`tests/index/test_rebuild_equivalence.cpp`; AV-001)
- [x] Linked project roots from `config.toml`, one watcher per root, `.gitignore` honoured (`src/vault/Roots.cpp`, `src/vault/GitIgnore.cpp`)
- [x] Incremental indexer driven by the file watchers on a worker thread (`src/vault/VaultWatcher.cpp`, `src/index/IndexService.cpp`; D-019)
- [x] Explorer shows each linked root as a top-level node, greyed when missing (`src/ui/ExplorerPanel.cpp`; F-034)
- [x] First-open offer to add `.coeus/` to the vault's `.gitignore` (`src/vault/GitIgnoreOffer.cpp`, `src/vault/VaultConfig.cpp`; SPEC.md §Vault layout)
- [x] Editor source mode with syntax highlighting and save through `AtomicWriter` (`src/ui/Editor.cpp`)
- [x] Autosave journal with recovery on startup (SPEC.md §Autosave journal)
- [x] Conflict prompt when a note with unsaved changes is modified on disk (`tests/ui/test_conflict_prompt.cpp`; F-011, AV-002)
- [x] Reading mode: rendered Markdown, switchable per tab (`src/parser/HtmlRenderer.cpp`, `src/ui/NoteView.cpp`; F-003)
- [x] Wikilink completer over titles, aliases and headings (`src/ui/WikilinkCompleter.cpp`, `src/ui/NoteCatalogue.cpp`; F-004)
- [x] Backlinks panel with unlinked mentions (`src/index/Backlinks.cpp`, `src/ui/BacklinksPanel.cpp`; F-005)
- [x] Outline panel (`src/ui/OutlinePanel.cpp`; F-013)
- [x] Tag pane (`src/index/Tags.cpp`, `src/ui/TagPanel.cpp`; F-007)
- [x] Search panel (`src/index/Search.cpp`, `src/ui/SearchPanel.cpp`; F-006)
- [x] Properties panel with typed validation (`src/ui/PropertiesPanel.cpp`, `src/registers/Vocabulary.cpp`; F-008)
- [x] Daily notes and templates (`src/vault/DailyNotes.cpp`, `src/vault/Templates.cpp`; F-009, F-010; `{{next:KIND}}` completed in Phase 3)
**Acceptance:** A real vault (the author's project repos) can be browsed, searched, edited and saved for a week without data loss or index drift (AV-001, AV-002).

## Phase 3 — Differentiators
**Goal:** The three features that make Coeus more than an Obsidian clone.
**Status:** Built (2026-10-14). Order agreed 2026-10-13: registers (F-015, F-016), native Git (F-020–F-022), the project board (F-032), the MCP server (F-033), then semantic search (F-018, F-019, F-035) once D-008 was decided, which it was on 2026-10-14: bge-small-en-v1.5, run on the CPU by llama.cpp inside Coeus. Every deliverable is built; the acceptance below is the author's to confirm
**Features delivered:** F-015, F-016, F-018, F-019, F-020, F-021, F-022, F-032, F-033, F-035; F-036 and F-037 added after it was built (2026-10-15)
**Deliverables:**
- [x] Register parser and model over the scaffold document types (`src/parser/RegisterParser.cpp`, `src/index/ProjectResolver.cpp`; index schema 2)
- [x] Register panel, ID completion, "Insert next ID" command and `{{next:KIND}}` (`src/ui/RegisterPanel.cpp`, `src/registers/NextId.cpp`, `src/registers/RegisterTemplates.cpp`; F-010, F-015)
- [x] Lint rules L1–L9, the Problems panel and editor underlines (`src/registers/Lint.cpp`, `src/index/LintResolver.cpp`, `src/ui/ProblemsPanel.cpp`; index schema 3; F-016)
- [x] Ideas register and project board with freshness, phase, unreleased and Git ahead/behind columns (`src/ui/ProjectBoard.cpp`, `src/registers/ProjectStatus.cpp`; index schema 4; F-032)
- [x] Refresh header and Promote idea (`src/registers/ProjectScaffold.cpp`, `src/ui/PromoteDialog.cpp`; F-032)
- [x] Embedding model running in Coeus: bge-small-en-v1.5 through llama.cpp, fetched by `tools/fetch_model.py` (`src/search/Embedder.cpp`; D-008)
- [x] Chunking, embedding worker and vector storage (`src/search/Chunker.cpp`, `src/search/EmbeddingService.cpp`, `src/search/VectorStore.cpp`; index schema 5; F-018)
- [x] Semantic and hybrid query modes in the search pane and MCP (F-018)
- [x] Related-notes sidebar and MCP `related` (`src/ui/RelatedPanel.cpp`, `SemanticSearch::related`; F-019)
- [x] Embeddings sidecar export, import and compaction (`src/search/Sidecar.cpp`; F-035)
- [x] New Note… from a template, which may choose its folder (`src/vault/NewNote.cpp`, `src/ui/NewNoteDialog.cpp`; F-036, added 2026-10-15)
- [x] Problems report: MCP `problems` and the panel's Copy Report (`src/registers/ProblemReport.cpp`; F-037, added 2026-10-15)
- [x] libgit2 wrapper, step 1: repository discovery, per-note history following renames, a file at a commit (`src/git/GitRepository.cpp`; F-020)
- [x] History sidebar section and read-only versions (`src/ui/HistoryPanel.cpp`, `src/ui/VersionView.cpp`; F-020)
- [x] libgit2 wrapper, step 2: diff of two texts, blame of a buffer (`src/git/GitRepository.cpp`; F-021)
- [x] Inline diff view and gutter blame (`src/ui/DiffView.cpp`, `src/ui/Editor.cpp`; F-021)
- [x] libgit2 wrapper, step 3: status and committing chosen paths; push by `git push` (`src/git/GitRepository.cpp`, `src/git/GitCommand.cpp`; D-020)
- [x] Status-bar change count and commit dialog (`src/ui/CommitDialog.cpp`; F-022)
- [x] Ahead/behind against the upstream for the project board (`GitRepository::tracking`; F-032)
- [x] Fetch from Coeus, so ahead/behind is current (`git fetch`, D-020 amended; F-032)
- [x] MCP server with read tools (`src/mcp/McpServer.cpp`, `src/mcp/McpTools.cpp`; F-033)
- [x] Opt-in MCP write tools with GUI confirmation and undo (`src/registers/EntryWriter.cpp`, `src/mcp/GuiLink.cpp`, `src/ui/McpLink.cpp`, `src/ui/WriteToast.cpp`; F-033, AV-012)
**Acceptance:** Opening the vault plus linked repos shows every register with correct counts; a semantic query for a decision by meaning finds it; a note's history, blame and diff match `git log`, `git blame`, `git diff` output; Claude Code in Antigravity can log a DECISIONS entry through MCP that appears in the register panel with the correct next ID.

## Phase 4 — Rich Markdown and workspace
**Goal:** Feature parity with the parts of Obsidian's core that Phase 2 deferred.
**Status:** Complete (2026-10-17): built 2026-10-16 (F-027, F-014, F-023, F-024, F-025 and F-003 on 2026-10-15; F-012 and F-026 on 2026-10-16) and accepted by the author 2026-10-17. Order agreed 2026-10-15: themes and hotkeys (F-027), so later renderers take their colours from the theme; bookmarks and recents (F-014); rich rendering in reading mode (F-025 code highlighting, F-023 embeds, F-024 block references, then Mermaid and maths); live preview (F-003), which reuses that rendering; splits and multi-window (F-012); graph view (F-026). How Mermaid and maths render (Qt WebEngine, native renderers, or not at all) is decided before that part of the rendering step: D-022, accepted 2026-10-15 (Option B)
**Features delivered:** F-003 (live preview), F-012, F-014, F-023, F-024, F-025, F-026, F-027
**Deliverables:**
- [x] Live-preview editing mode
  - [x] Step 1: hidden syntax, folding, pictures in place, Ctrl+click, Default Mode (`src/ui/LivePreview.cpp`, `src/parser/LiveBlocks.cpp`; F-003)
  - [x] Step 2: tables, callouts and note embeds drawn as blocks, from reading mode's renderer (F-003)
- [x] Splits and native multi-window (D-023)
  - [x] Step 1: panes of tabs in splitters (`src/ui/Workspace.cpp`; F-012)
  - [x] Step 2: several windows sharing the vault's session, each with its own docks and layout (`src/ui/VaultSession.cpp`; F-012)
- [x] Embeds, block references, callouts, Mermaid, maths, code highlighting
  - [x] Code highlighting by language and note and section embeds in reading mode (`src/parser/CodeHighlighter.cpp`, `src/parser/Transclusion.cpp`; F-025, F-023)
  - [x] Block references: links, completion, embeds and Copy Block Link (`src/parser/Transclusion.cpp`; F-024)
  - [x] Mermaid, maths and PDF pages inline: optional external renderers, cached, and Qt PDF (`src/render/`, `src/ui/RenderService.cpp`; D-022)
- [x] Graph view: global as a tab, local in the sidebar (`src/index/LinkGraph.cpp`, `src/ui/ForceLayout.cpp`, `src/ui/GraphView.cpp`, `src/ui/GraphPanel.cpp`; F-026)
- [x] Themes and rebindable hotkeys (`src/ui/Theme.cpp`, `src/ui/Shortcuts.cpp`, `src/ui/ShortcutsDialog.cpp`; F-027)
- [x] Bookmarks and recent notes (`src/vault/Bookmarks.cpp`, `src/vault/RecentNotes.cpp`, `src/ui/BookmarksPanel.cpp`; F-014)
**Acceptance:** A note using every rich construct renders identically in live preview and reading mode; two windows on two monitors keep independent dock layouts.

## Phase 5 — Sync
**Goal:** Vault available on a second Linux machine with conflict safety.
**Status:** In progress. Order agreed 2026-10-17: rclone first (F-028), the native Google Drive provider (F-029) decided after living with it (D-007, D-024). F-028 step 1 built 2026-10-17
**Features delivered:** F-028, F-029
**Deliverables:**
- [ ] `SyncProvider` interface and rclone implementation over root 0 only, `.coeus/` excluded, `.coeus-shared/` included (`src/sync/`)
  - [x] Step 1: the provider, passes on a timer and on command, status bar, join and Sync Anyway prompts (`src/sync/RcloneProvider.cpp`, `src/sync/SyncService.cpp`; D-024)
  - [ ] Step 2: resolving conflicts in the window
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
