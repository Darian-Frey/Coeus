# Architecture

Descriptive only. Rationale lives in DECISIONS.md.

## System overview

```
                 ┌──────────────────────────────────────────────┐
                 │                    ui/                        │
                 │  MainWindow · Editor · Explorer · Sidebar     │
                 │  CommandPalette · RegisterPanel · GitPanel    │
                 └───────▲──────────────▲──────────────▲─────────┘
                         │ signals      │ queries      │ commands
                         │              │              │
        ┌────────────────┴───┐   ┌──────┴──────┐  ┌────┴─────────────┐
        │      vault/        │   │   search/   │  │      git/        │
        │ Vault · Watcher    │   │ FullText    │  │ Repo · History   │
        │ AtomicWriter       │   │ Semantic    │  │ Diff · Blame     │
        └────────┬───────────┘   └──────┬──────┘  └──────────────────┘
                 │ file events          │ reads
                 ▼                      ▼
        ┌────────────────────┐   ┌─────────────────────────────────┐
        │      parser/       │──▶│             index/              │
        │ Markdown · Links   │   │ SQLite: notes links tags props  │
        │ Frontmatter        │   │ registers fts5 vec0             │
        │ Registers          │   └─────────────────────────────────┘
        └────────────────────┘             ▲
                                           │ register model
        ┌────────────────────┐   ┌─────────┴───────┐   ┌──────────────┐
        │       sync/        │   │   registers/    │   │  Mnemosyne   │
        │ SyncProvider       │   │ Model · Lint    │   │ (external)   │
        │ Rclone · Drive     │   │ NextId          │   │ embeddings   │
        └────────────────────┘   └─────────────────┘   └──────────────┘

   Vault folder + linked roots (source of truth)  ──▶  .coeus/index.db (derived, rebuildable)
   Vault folder ──▶ Google Drive (sync)      Linked roots ──▶ GitHub (git push)
   .coeus-shared/embeddings.bin travels with the vault; .coeus/ never does

        ┌────────────────────┐
        │       mcp/         │◀── Claude chat · Claude Code (Antigravity) · Aporia
        │ read + write tools │──▶ same index, same AtomicWriter and NextId
        └────────────────────┘
```

Data flows one way: filesystem → parser → index → UI. The UI writes back to the filesystem only through `vault/AtomicWriter`, which re-enters the same pipeline via the watcher.

## Module responsibilities

**app/** — `main()`, `QApplication`, settings file, command-line parsing, wiring modules together. Owns nothing else. `coeus --lint` takes a `QCoreApplication` instead, so it runs without a display, and hands over to `registers/LintCommand`.

**ui/** — All Qt widgets. `MainWindow` is a `QMainWindow` whose central widget is a `QTabWidget` of `Editor` instances; the explorer and sidebar are `QDockWidget`s. `ExplorerPanel` holds one collapsible section per root, each a `QTreeView` over its own `QFileSystemModel`, and a flat filtered list shown in their place when a tag is active. `TagPanel` and `SearchPanel` are left docks tabbed with the explorer. Each tab is a `NoteView`: a breadcrumb and mode switch over the source `Editor` and a reading `QTextBrowser`. `Editor` is a `QPlainTextEdit` that saves through `Vault::writeNote` and refuses to overwrite a changed file (SPEC.md §Editing); `MarkdownHighlighter` colours source mode; `WikilinkCompleter` offers `[[` completions from `NoteCatalogue`, a background-loaded snapshot of the index's notes and aliases, ranked by the pure `WikilinkCompletion`. `RegisterPanel` is a left dock tabbed with the explorer, listing one project's register entries from the same catalogue, which also feeds the completer's register-ID completions; `ProblemsPanel` is a bottom dock listing the register lint from the catalogue, and each `Editor` underlines its own note's problems; `BacklinksPanel`, `RelatedPanel` (the notes nearest in meaning that it does not link to, with Link here, F-019), `HistoryPanel` (the note's commits, F-020) and `OutlinePanel` are the sidebar's sections, a `VersionView` tab shows a note as it was in a commit and a `DiffView` tab compares two versions, both read-only, and each `Editor` can show a blame gutter beside its text (F-021); `CommitDialog` commits the changes the author ticks, and the status bar counts the changed files (F-022); `ProjectBoard` is a bottom dock listing projects and ideas with their freshness, build status and Git state, with Refresh header, Promote idea (`PromoteDialog`) and Fetch (F-032); `PropertiesPanel` sits above each tab's editor (F-008), editing frontmatter one key's lines at a time through `Frontmatter::setValue`; the outline parses the editor's buffer rather than reading the index, so it shows unsaved headings. Saves run on the UI thread (one small atomic write); journal writes run on a one-thread pool owned by `MainWindow`. Panels subscribe to index signals and issue queries; they never touch the filesystem or SQLite directly.

**vault/** — Filesystem boundary. `Vault` resolves paths and enumerates files; `TempSweeper` removes temporary files left by interrupted saves, on a worker thread; `VaultWatcher` puts one inotify watch on each folder of a root (D-019) and reports changed notes by path, rename-based saves included; `Roots` reads linked roots from `config.toml` (D-016); `GitIgnore` applies a root's Git ignore rules through libgit2; `GitIgnoreOffer` and `VaultConfig` implement the first-open `.gitignore` offer and line-preserving `config.toml` edits; `AtomicWriter` performs temp-write, fsync, rename; `Journal` keeps unsaved buffers in `.coeus/journal/`; `DailyNotes` and `Templates` create daily notes and render and insert templates.

**parser/** — Pure functions from text to structures. `NoteParser` runs md4c (D-014) and `Frontmatter` runs yaml-cpp (D-015) to yield a `ParsedNote`: links (wiki and Markdown), embeds, tags, headings, block IDs, properties and plain text, each with a file line number. `HtmlRenderer` renders a note to HTML for reading mode from the same md4c callbacks (D-014). `RegisterParser` recognises the scaffold document types by filename and yields typed entries and the README status header. No Qt dependencies beyond `QString`; unit-testable in isolation.

**index/** — `Database` (RAII over the SQLite C API, D-018), `Schema` (rebuilt rather than migrated), `IndexStore` (row writes per note), `ProjectResolver` (projects from status headers, note and register membership, citation targets), `LintResolver` (runs the register lint after it and stores the problems), `IndexQueries` (read-only lookups for the UI, one short connection each), `Backlinks` (links to a note and unlinked mentions, with context lines), `Tags` (the nested tag tree and notes per tag), `LinkResolver` (SPEC.md §Wikilink resolution) and `Indexer`, which scans a root, applies single-file updates and verifies against disk, one transaction per call. `ContentHash` is BLAKE2b-128 (D-017). The worker thread that feeds `Indexer` from watcher events arrives with the watcher. Exposes read-only query objects to the UI and emits `noteChanged`, `linksChanged`, `registersChanged` signals.

**registers/** — Domain model over index rows: status vocabularies (`Vocabulary`, in use by the properties panel since Phase 2), `RegisterTemplates` (register file names, file skeletons and entry skeletons per kind), `ProjectStatus` (a project's build status from its ROADMAP and CHANGELOG), `ProjectScaffold` (the text of a refreshed header and of a promoted idea's files), `EntryWriter` (a register entry written to the standard, and where it goes), the `NextId` allocator (SPEC.md §ID allocation; one `BEGIN IMMEDIATE` transaction on `index.db`, run off the UI thread), and `Lint`, the rules of SPEC.md §Lint rules as pure functions (unresolved references, superseded targets, invalid status, unidirectional cross-references).

**search/** — Full-text search landed in `index/` as `SearchQuery` (parser) and `Search` (one SQL statement per query, with a `regexp` function registered on the read-only connection), since it is only queries over the index. Semantic search (F-018, D-008) lives here: `Chunker` splits notes into passages at index time (the `Indexer` calls it; no model needed), `Embedder` runs bge-small-en-v1.5 on the CPU through llama.cpp, `VectorStore` keeps one vector per distinct passage and embeds what is missing, `SemanticSearch` scans the vectors for a query and blends with the word search, and finds a note's related notes from its stored vectors alone, and `EmbeddingService` is the window's embedding worker on its own thread. `Sidecar` (F-035) is still to come.

**git/** — Thin C++ wrapper over libgit2: repository discovery per path, per-file log, blob retrieval, diff, blame, staging, commit, push. `GitRepository` does discovery, per-note history (renames followed), a file at a commit, blame of a buffer, the diff of two texts, status, committing chosen paths, and ahead/behind against the upstream. Push and fetch are the exceptions to libgit2: `GitCommand` runs `git push` or `git fetch` as a `QProcess` so the author's credential helper applies (D-020). It blocks and is not thread-safe, so each task on the Git worker opens its own; callers get results through `QFutureWatcher`.

**sync/** — `SyncProvider` interface (SPEC.md §Sync). `RcloneProvider` shells out to `rclone bisync`. `DriveProvider` (Phase 5) talks to the Google Drive API with a change log and merge engine. Providers see encrypted bytes when encryption is enabled. Providers operate on root 0 only; linked roots are assumed to sync through Git.

**mcp/** — `coeus --mcp` entry point. `McpServer` speaks the protocol (JSON-RPC over stdio) and keeps the vault's index current with the same `IndexRoots` reconciliation and incremental `Indexer` scans the GUI uses; `McpTools` implements the tools in SPEC.md §MCP surface. Read tools query the index read-only and work without the GUI. `GuiLink` is the server's side of a local socket to a running window (`ui/McpLink` the window's), used to refuse notes open with unsaved changes and to report writes, which the window offers to undo in a `WriteToast`. Write tools go through `vault/AtomicWriter` and `registers/NextId` exactly as the UI does, and, when the GUI is running, through a local socket to raise the confirmation toast.

**vault/Roots** — reads `[[roots]]` from `config.toml`, assigns `root_id`s, owns one `VaultWatcher` per present root, and marks absent roots without deleting their rows (D-010).

**search/Sidecar** — exports and imports `.coeus-shared/embeddings.bin` (SPEC.md §Embeddings sidecar) so a second machine inherits vectors by content hash instead of recomputing (D-011).

**Mnemosyne** — External subsystem providing local embeddings. Coeus links against it or talks to it over a local socket (open under D-008).

## Key invariants

1. **Vault is truth, index is cache.** Deleting `.coeus/index.db` and restarting must reproduce an identical index with no user-visible change beyond a rebuild delay. (AV-001)
2. **Writes are explicit and atomic.** Only `AtomicWriter` touches vault files, and only on behalf of a user action. (AV-002) The one exception is `TempSweeper`, which deletes `AtomicWriter`'s own stale temporary files on vault open (IMP-002).
3. **UI thread never blocks.** Indexing, embedding, Git and sync run on worker threads; the UI observes signals. (AV-005)
4. **Register IDs are append-only.** `NextId` reads the highest existing ID and adds one; nothing renumbers. (AV-003)
5. **One vector space per index.** The embedding model name and dimension are stored in the index; a mismatch triggers a full re-embed, never a mixed table. (AV-007)
6. **Parser is pure.** Same input text always yields the same structures; no filesystem or clock access inside `parser/`.
7. **`.coeus/` never leaves the machine; `.coeus-shared/` holds only hash-keyed derived data.** The index is rebuilt per machine; only the embeddings sidecar syncs (D-011).
8. **Every write path is the same write path.** UI save, template insertion, idea promotion and MCP write tools all go through `AtomicWriter`, and every ID through `NextId`. No module writes a vault file any other way (AV-012).

## Cross-cutting concerns

**Threading.** One indexer worker, one embedding worker, one Git worker, one sync worker, each a `QThread` with a queue. The indexer worker is `index/IndexService`: it owns the index database, the `Indexer`, and each root's `GitIgnore` and `VaultWatcher`, and `MainWindow` starts it when a vault opens. The embedding worker is `search/EmbeddingService` on a low-priority `QThread`, woken by every index change; it keeps its own connection and its own copy of the model, and the search pane has another copy on a one-thread pool, so a query never waits behind a batch. The Git worker is a one-thread `QThreadPool` owned by `MainWindow`, so libgit2 calls run one at a time, in order; panels submit tasks with `QtConcurrent::run` and take results through `QFutureWatcher`, dropping any a newer request has overtaken. Communication is signal/slot with queued connections. SQLite is opened per thread with WAL mode; the UI thread holds read-only connections.

**Error handling.** Exceptions do not cross module boundaries. Modules return `std::expected<T, Error>` (C++23, D-013); the UI maps errors to status-bar messages or dialogs. A failed index update for one file never aborts the batch.

**Logging.** `QLoggingCategory` per module (`coeus.vault`, `coeus.index`, …). Default level warning; `--verbose` raises to debug. Logs go to stderr and a rotating file under `.coeus/logs/`.

**Settings.** `QSettings` in INI format at the XDG config path for app-level settings; per-vault settings in `.coeus/config.toml`.

**Testing.** Parser and registers are covered by unit tests with no Qt event loop. Index and watcher tests run against a temporary vault fixture. UI tests are limited to smoke tests until Phase 4.
