# Features

## Target users
A solo developer-researcher who keeps personal notes and a dozen project repositories, each documented to the project-scaffold standard, and who wants one Linux desktop tool that reads them all as a single linked vault.

## Out of scope
- Publishing a vault as a website (Obsidian Publish equivalent).
- Mobile or non-Linux builds.
- A third-party plugin system in v1. Functionality that Obsidian delegates to plugins is either built in (see F-015 onward) or not offered.
- Real-time collaborative editing.
- Proprietary note formats. Everything is `.md` plus ordinary attachments.

## Features

### Vault and shell

### F-001 Application shell
**Priority:** Must
**Acceptance:**
- `QMainWindow` with central editor, left explorer dock, right sidebar dock, toolbar, status bar, tabbed notes
- Dock layout and window geometry persist across restarts
- Command palette opens on `Ctrl+P` and lists all registered actions with fuzzy filtering
**Status:** Complete (Phase 1, 2026-10-12)
**Related decisions:** D-002, D-004
**Notes:** Every later feature is a panel or command inside this shell. See ARCHITECTURE.md §UI.

### F-002 Open a vault
**Priority:** Must
**Acceptance:**
- Any folder can be opened as a vault from the command line or File menu
- File explorer shows the folder tree filtered to `.md` and attachment types
- A 1,000-file vault renders its tree in under 500 ms on target hardware
**Status:** Complete (Phase 1, 2026-10-12)
**Related decisions:** D-005, D-010

### F-003 Markdown editor with three modes
**Priority:** Must
**Acceptance:**
- Source, live-preview, and reading modes switchable per tab
- Syntax highlighting for CommonMark plus wikilinks, tags, frontmatter, callouts
- Saves are atomic (AV-002); unsaved changes survive a crash via an autosave journal
**Status:** Complete (Phase 4, 2026-10-15): source mode, atomic save, autosave journal and reading mode in Phase 2; live preview in Phase 4: the cursor's lines shown as written, the rest styled with its syntax hidden, frontmatter and fences folded, pictures, tables, callouts and note embeds drawn in place, Ctrl+click on links, and View › Default Mode, Live at first (SPEC.md §Live preview)
**Related decisions:** D-002, D-014
**Notes:** Live preview is the single most expensive feature in the project. D-004 records the decision to start with source and reading modes and add live preview once the shell is stable.

### F-004 Wikilinks with autocomplete
**Priority:** Must
**Acceptance:**
- `[[Note]]`, `[[Note|alias]]`, `[[Note#Heading]]`, `[[Note^block]]` all resolve
- Typing `[[` opens a completer over note titles, aliases, and headings
- Ambiguous names (two notes with the same title in different folders) are resolved by nearest-folder rule then flagged (AV-006)
**Status:** Complete (Phase 2, 2026-10-13)
**Related decisions:** D-014

### F-005 Backlinks and unlinked mentions
**Priority:** Must
**Acceptance:**
- Right sidebar lists every note linking to the current one, with context line
- Unlinked mentions section lists plain-text occurrences of the note's title
- Clicking a backlink opens the source note at the linking line
**Status:** Complete (Phase 2, 2026-10-13)

### F-006 Full-text search
**Priority:** Must
**Acceptance:**
- SQLite FTS5 over note bodies with `tag:`, `path:`, `file:`, and quoted-phrase operators
- Results update as-you-type with under 100 ms latency on a 10,000-note vault
**Status:** Complete (Phase 2, 2026-10-13)
**Related decisions:** D-018

### F-007 Tags
**Priority:** Must
**Acceptance:**
- Inline `#tag` and frontmatter `tags:` both indexed; nested `#a/b` supported
- Tag pane shows counts and filters the explorer
**Status:** Complete (Phase 2, 2026-10-13)
**Related decisions:** D-014

### F-008 Typed properties (frontmatter)
**Priority:** Must
**Acceptance:**
- YAML frontmatter rendered as a properties panel above the note
- Property types: text, number, date, list, enum; enum values are those the author declares in the vault's `Coeus properties.md`, by key and folder, and the idea stages for `stage` under `ideas/` (amended 2026-10-17, D-025: `status:` was first validated against the project statuses everywhere)
- Invalid values shown as errors, never silently rewritten; other text properties suggest the values the vault already uses
**Status:** Complete (Phase 2, 2026-10-13); declared values and suggestions 2026-10-17 (D-025)
**Related decisions:** D-015, D-025

### F-009 Daily notes
**Priority:** Should
**Acceptance:**
- `Ctrl+D` opens or creates today's note at a configurable path pattern using ISO 8601 date
- Optional template applied on creation
**Status:** Complete (Phase 2, 2026-10-13)

### F-010 Templates
**Priority:** Should
**Acceptance:**
- Templates folder; insert via command palette
- Variables: `{{date}}`, `{{time}}`, `{{title}}`, `{{next:F}}` (see F-015)
**Status:** Complete (2026-10-13): the templates folder, insertion and `{{date}}`, `{{time}}`, `{{title}}` in Phase 2; `{{next:KIND}}` allocates real IDs through `NextId` in Phase 3

### F-011 File watcher
**Priority:** Must
**Acceptance:**
- External edits (git pull, another editor, Claude Code) appear in the open tab within one second
- Rename-based saves (delete + create) handled as modifications
- Conflicting unsaved edits prompt rather than clobber (AV-002)
**Status:** Complete (Phase 2, 2026-10-12)
**Related decisions:** D-005, D-010, D-019

### F-012 Tabs and split panes
**Priority:** Should
**Acceptance:**
- Multiple notes open in tabs; horizontal and vertical splits
- Native multi-window: a note can be torn off into its own `QMainWindow` with its own docks
**Status:** Complete (Phase 4, 2026-10-16): step 1 built 2026-10-15, panes of tabs in splitters, Split Right and Split Down, moving tabs and focus between panes, a note open in one place at a time; step 2 built 2026-10-16, Move to New Window, several windows sharing the vault's session, each with its own docks and saved layout (D-023, SPEC.md §Panes)
**Related decisions:** D-002

### F-013 Outline panel
**Priority:** Should
**Acceptance:**
- Heading tree for the current note; click scrolls the editor
**Status:** Complete (Phase 2, 2026-10-13)
**Related decisions:** D-014

### F-014 Bookmarks and recent files
**Priority:** Could
**Acceptance:**
- Star any note or search; recents list in the command palette
**Status:** Complete (Phase 4, 2026-10-15): ☆ in each note's header and beside the Search pane's query, Toggle Bookmark, a Bookmarks pane, Open Recent Note… in a palette and File › Open Recent; kept per vault and machine in `.coeus/` (SPEC.md §Bookmarks and recent notes)

### Scaffold-aware registers

### F-015 Register objects
**Priority:** Must
**Acceptance:**
- Any `FEATURES.md`, `DECISIONS.md`, `CLAIMS.md`, `ATTACK_VECTORS.md`, `BUGS.md`, `IMPROVEMENTS.md` in the vault is parsed into typed entries (ID, title, status, fields) per SPEC.md §Registers
- A register panel lists entries per project with status filters and sort by ID
- Typing `F-`, `D-`, `C-`, `AV-`, `BUG-`, `IMP-` in the editor offers completion over known IDs with their titles
- Command "Insert next ID" appends the next free ID for the chosen register in the current project (append-only, AV-003)
**Status:** Complete (Phase 3, 2026-10-13): entries, projects and citations are indexed; the register panel, ID completion and Insert Next ID are built (SPEC.md §Register panel and commands). Validation is F-016
**Related decisions:** D-001, D-006, D-012, D-025
**Notes:** Nothing else in the tool depends on the scaffold; a vault with no register files behaves as a plain Obsidian-style vault.

### F-016 Register validation
**Priority:** Must
**Acceptance:**
- Unresolved ID references are underlined in the editor and listed in a Problems panel
- References to entries with status Superseded / Withdrawn / Refuted / Deprecated are flagged as warnings
- Status values outside the fixed vocabularies are flagged as errors
- Unidirectional cross-references (D cites AV but AV does not cite D) are reported, satisfying Maintenance Rule 6 of the standard
**Status:** Complete (Phase 3, 2026-10-13): lint rules L1–L9, the Problems panel and editor underlines (SPEC.md §Lint rules, §Problems panel)
**Related decisions:** D-006

### F-017 Review-freshness dashboard
**Priority:** Should
**Acceptance:**
- Reads the four-field README status header from every project folder
- Dashboard lists projects by Status with Last reviewed age; entries older than a configurable threshold (default 14 days) are highlighted
- One-click "Refresh header" updates Last reviewed to today after confirmation
**Status:** Withdrawn — absorbed into F-032 (2026-10-12)
**Related decisions:** D-006
**Notes:** Kept for ID stability. The freshness view is one column of the project board in F-032.

### Semantic search and related notes

### F-018 Semantic search
**Priority:** Must
**Acceptance:**
- Notes are chunked and embedded locally (D-008: bge-small-en-v1.5 through llama.cpp); vectors stored in the index (`sqlite-vec` at first; a plain table by D-008's amendment)
- A semantic query mode in the search panel returns nearest chunks with note and heading context
- Embedding runs on a worker thread; the UI never waits on it (AV-005)
- Changing the embedding model triggers a full re-embed rather than mixing vector spaces (AV-007)
**Status:** Complete (Phase 3, 2026-10-14): chunking at index time, the embedding worker, vectors per passage, and Words, Meaning and Both in the search pane and MCP's `search` (SPEC.md §Embeddings)
**Related decisions:** D-001, D-006, D-011, D-012, D-017, D-018
**Notes:** Model choice is D-008 (pending).

### F-019 Related-notes sidebar
**Priority:** Must
**Acceptance:**
- Right sidebar section shows the five nearest notes to the current note's content, excluding notes already linked from it
- Updates on save, debounced, without blocking editing
- Each entry offers "Link here" which inserts a wikilink at the cursor
**Status:** Complete (Phase 3, 2026-10-14): a Related section between Backlinks and History, recomputed off the UI thread when the tab changes, after a save, and as embedding progresses (debounced 800 ms); Link here inserts the link the completer would; MCP `related`. Boilerplate shared across projects is discounted (SPEC.md §Embeddings)
**Related decisions:** D-006, D-008

### Native Git

### F-020 Per-note history
**Priority:** Must
**Acceptance:**
- If the vault (or a subfolder) is a Git repository, the sidebar shows the current note's commit history via libgit2
- Selecting a commit shows that version read-only in a new tab
**Status:** Complete (Phase 3, 2026-10-13): History sidebar section and read-only versions through libgit2, renames followed (SPEC.md §Git)
**Related decisions:** D-001, D-006, D-010

### F-021 Diff and blame
**Priority:** Must
**Acceptance:**
- Diff any two versions of a note, or working copy against HEAD, inline in the editor
- Gutter blame toggle shows commit and date per line
**Status:** Complete (Phase 3, 2026-10-14): inline diff tabs (working copy or HEAD or any commit against another) and a blame gutter of the live buffer (SPEC.md §Git)
**Related decisions:** D-006

### F-022 Commit from Coeus
**Priority:** Should
**Acceptance:**
- Status bar shows dirty-file count; click opens a commit dialog with a multi-paragraph message editor
- Stage selected notes, commit, and optionally push; errors surface without blocking the UI
**Status:** Complete (Phase 3, 2026-10-14): changed-files count, commit dialog committing the ticked paths through libgit2, push by `git push` (D-020; SPEC.md §Git)
**Related decisions:** D-006

### Rich Markdown

### F-023 Embeds and transclusion
**Priority:** Should
**Acceptance:**
- `![[Note]]`, `![[Note#Heading]]`, `![[image.png]]`, `![[doc.pdf]]` render inline in preview and reading modes
**Status:** Complete (Phase 4, 2026-10-15): note, section and block embeds render inline in reading mode, resolved through the index, nested three deep; PDF pages inline through Qt PDF (D-022); audio, video and canvases are cards that open the file. Preview mode arrives with live preview (F-003)
**Related decisions:** D-014

### F-024 Block references
**Priority:** Could
**Acceptance:**
- `^block-id` suffixes are indexed and linkable; unnamed blocks get an ID on demand
**Status:** Complete (Phase 4, 2026-10-15): block and heading links open at their line; `[[Note#^` completes block IDs; block embeds render inline; Copy Block Link gives a paragraph or list item an ID on demand (SPEC.md §Block references)
**Related decisions:** D-014

### F-025 Callouts, Mermaid, LaTeX, code highlighting
**Priority:** Should
**Acceptance:**
- `> [!note]` family renders as styled blocks; Mermaid diagrams and KaTeX-style maths render in preview; fenced code highlighted by language
**Status:** Complete (Phase 4, 2026-10-15) in reading mode: callouts, fenced code coloured by language in each theme, maths through LaTeX and Mermaid through a headless browser when the machine has them, cached, else code (D-022). Live preview reuses them (F-003)
**Related decisions:** D-014

### F-026 Graph view
**Priority:** Could
**Acceptance:**
- Global and local link graphs with force layout; nodes coloured by project or tag; click opens note
**Status:** Complete (Phase 4, 2026-10-16): View › Open Graph View as a tab and the sidebar's local graph, tags as nodes, orphans, unresolved links and linked roots on request (SPEC.md §Graph view)

### F-027 Themes and hotkeys
**Priority:** Should
**Acceptance:**
- Light and dark themes; user QSS snippets; all actions rebindable
**Status:** Complete (Phase 4, 2026-10-15): System, Light, Dark and Cassette (an amber CRT terminal, at the author's request) themes that every view follows, live; style snippets from `~/.config/coeus/snippets/`; every registered command rebindable in View › Keyboard Shortcuts…, conflicts refused (SPEC.md §Themes and shortcuts); text zoom for the notes (View › Zoom In, Zoom Out, Reset Zoom, Ctrl+wheel) added at the author's request 2026-10-18; Dark Cyan, the main-window mockup's look, added at the author's request 2026-10-18

### Sync

### F-028 Sync provider layer
**Priority:** Must
**Acceptance:**
- `SyncProvider` interface with push/pull/conflict-report methods (SPEC.md §Sync)
- First implementation shells out to `rclone bisync` on a timer against a configured remote
- Sync status and last-sync time in the status bar
**Status:** In progress (Phase 5): step 1 built 2026-10-17, `SyncProvider` with one `sync()` pass, `RcloneProvider` (bisync, this machine's copy wins, the other kept as a conflict copy, the first resync guarded), passes on a timer and Sync Now, Set Up Sync…, the status-bar button and the join and Sync Anyway prompts (D-024, SPEC.md §Sync). Step 2: resolving conflicts in the window
**Related decisions:** D-003, D-007, D-011

### F-029 Native Google Drive sync
**Priority:** Should
**Acceptance:**
- OAuth sign-in; per-note change log; three-way merge on conflict with a resolution UI
- Optional client-side encryption so Drive holds only ciphertext
- Version history per note retrievable from Drive
**Status:** Not started (Phase 5): to be decided after living with the rclone provider (D-007, D-024), whose conflict copies it would keep
**Related decisions:** D-003, D-011, D-017

### Later

### F-030 Canvas
**Priority:** Could
**Acceptance:**
- Infinite whiteboard mixing note cards, text cards, and images, stored as a JSON file in the vault
**Status:** Not started (Phase 6)
**Related decisions:** D-005

### F-031 Bases-style table views
**Priority:** Could
**Acceptance:**
- Table and card views over notes filtered and sorted by properties, saved as view files
**Status:** Not started (Phase 6)
**Related decisions:** D-005

### Project tracking and AI integration

### F-032 Ideas register and project board
**Priority:** Must
**Acceptance:**
- An `ideas/` folder in the vault; each idea is a note with `stage`, `codename`, `repo` frontmatter per SPEC.md §Ideas
- Project board panel lists ideas by stage and live projects by README status, with Last reviewed age and Git ahead/behind per project
- Entries with Last reviewed older than `review_threshold_days` are highlighted; "Refresh header" updates the date after confirmation (absorbs F-017)
- "Promote idea" creates a project folder from the idea note and the standard's skeletons, allocating no IDs until the author adds entries
- Build status per project derives from ROADMAP.md phase status and CHANGELOG.md `[Unreleased]`; nothing is maintained by hand
**Status:** Complete (Phase 3, 2026-10-14): ideas indexed; the board with freshness, build, unreleased and Git columns; Refresh header, Promote idea and Fetch by `git fetch` (D-020; SPEC.md §Project board, §Ideas)
**Related decisions:** D-006, D-015, D-025
**Notes:** Decision D-010 provides the linked roots that let repos appear on the board.

### F-033 Coeus MCP server
**Priority:** Must
**Acceptance:**
- `coeus --mcp` serves the tools in SPEC.md §MCP surface over stdio against the current vault's index
- Read tools (`search`, `related`, `get_note`, `list_registers`, `project_status`) work with Coeus closed
- Write tools (`next_id`, `append_entry`, `write_note`) are disabled by default, enabled per vault in `config.toml`, and route through `AtomicWriter` and `NextId`
- When the GUI is running, each write tool call shows a confirmation toast with an undo for 10 s
- Claude Code in the Antigravity IDE and Claude chat can both be configured against it with a documented snippet in BUILD.md
**Status:** Complete (Phase 3, 2026-10-14): read tools working with Coeus closed; write tools `next_id`, `append_entry`, `write_note`, off unless enabled, refusing notes open with unsaved changes, with a 10 s undo toast when the window is open; BUILD.md snippets. `related` arrived with F-019. Claude Code is the client it is built for, and claude.ai in a browser cannot reach a local server (D-021)
**Related decisions:** D-012

### F-034 Linked project roots
**Priority:** Must
**Acceptance:**
- `[[roots]]` entries in `.coeus/config.toml` are indexed alongside the vault with their own watchers
- Explorer shows each root as a top-level node; a root missing on this machine is greyed, not removed
- Wikilink resolution prefers same-root matches (SPEC.md §Wikilink resolution)
- Linked roots are excluded from the sync provider (F-028)
**Status:** Complete (Phase 2, 2026-10-12). The sync exclusion holds by construction (SPEC.md §Sync: providers see root 0 only) and is tested when F-028 lands
**Related decisions:** D-016, D-019
**Notes:** Pulled into Phase 2 because the watcher and index schema must carry `root_id` from the start.

### F-035 Embeddings sidecar
**Priority:** Should
**Acceptance:**
- `.coeus-shared/embeddings.bin` exported after each embedding batch, keyed by chunk hash and tagged with model and dimension
- On startup, matching hashes are imported before any local embedding runs; mismatched model or dimension is ignored with a log line
- A compaction command drops hashes no longer present in the vault
**Status:** Complete (Phase 3, 2026-10-14): `search/Sidecar`; imported before embedding and exported, as a whole-file atomic union, when an embedding pass ends (SPEC.md §Embeddings sidecar); only vectors of the model in use (D-008) imported; conflict copies united and removed; File › Compact Embeddings Sidecar and a weekly compaction; `coeus --mcp` imports and exports too
**Related decisions:** D-017

### F-036 New note from a template
**Priority:** Should
**Acceptance:**
- File › New Note… (`Ctrl+N`) asks for a name, a folder (in the vault or a linked root) and a template, Blank or any in the templates folder (F-010)
- It starts in the folder selected in the explorer, else the current note's, else the vault's top level
- A template may name the folder its notes go in with a `coeus-folder` frontmatter key, which never reaches a note
- The note is rendered with the template's variables, `{{next:KIND}}` allocated like Insert Template, written atomically and opened; an existing note is never overwritten
**Status:** Complete (2026-10-15): `vault/NewNote`, `ui/NewNoteDialog`, `Templates::placement` (SPEC.md §Daily notes and templates)
**Notes:** Asked for by the author 2026-10-15, between Phases 3 and 4; until then Coeus could not make a note except today's.

### F-037 Problems report for AI sessions
**Priority:** Should
**Acceptance:**
- An MCP tool `problems` returns the register lint's problems (F-016) for a project, or every project, filtered by level, with file, line, rule and what each rule means (F-033)
- The Problems panel's Copy Report puts exactly what it shows on the clipboard as Markdown, grouped by file with `path:line` references relative to the project
**Status:** Complete (2026-10-15): `registers/ProblemReport`, shared by both (SPEC.md §Problems panel, §MCP surface)
**Notes:** Asked for by the author 2026-10-15 as a report file for Claude Code. A file goes stale as problems are fixed and lands in the repository; Claude Code reaches the vault through MCP already, so it asks for the current problems instead, and Copy Report covers handing them over by hand.

## Candidate features (uncommitted)
- GitHub API status: pull-request state and Actions results on the project board, via polling or webhook.
- Crucible mode: structured adversarial-review view that spawns a review note and links findings back as AV- entries.
- LLM hooks: send a selection to a local or remote model with a prompt template, returning a linked note with provenance frontmatter.
- Aporia integration: an open-items panel that is a live query rather than a maintained list.
- Subtle Chaos lint for commit messages drafted in F-022.
- Vault snapshots independent of Git.
- Web clipper.
