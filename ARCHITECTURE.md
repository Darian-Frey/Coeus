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

**app/** — `main()`, `QApplication`, settings file, command-line parsing, wiring modules together. Owns nothing else.

**ui/** — All Qt widgets. `MainWindow` is a `QMainWindow` whose central widget is a `QTabWidget` of `Editor` instances; the explorer and sidebar are `QDockWidget`s. Panels subscribe to index signals and issue queries; they never touch the filesystem or SQLite directly.

**vault/** — Filesystem boundary. `Vault` resolves paths and enumerates files; `VaultWatcher` wraps `QFileSystemWatcher` on directories and normalises rename-based saves into modify events; `AtomicWriter` performs temp-write, fsync, rename.

**parser/** — Pure functions from text to structures. CommonMark plus wikilinks, tags, frontmatter, callouts, block IDs. `RegisterParser` recognises the scaffold document types by filename and yields typed entries. No Qt dependencies beyond `QString`; unit-testable in isolation.

**index/** — SQLite schema (SPEC.md §Index), migrations, and `Indexer`, which consumes watcher events on a worker thread and updates rows incrementally. Exposes read-only query objects to the UI and emits `noteChanged`, `linksChanged`, `registersChanged` signals.

**registers/** — Domain model over index rows: `RegisterEntry`, `RegisterKind`, status vocabularies, `NextId` allocator, `Lint` rules (unresolved references, superseded targets, invalid status, unidirectional cross-references).

**search/** — `FullText` (FTS5 query builder with operators) and `Semantic` (chunking, embedding requests to Mnemosyne, `vec0` nearest-neighbour queries, related-notes ranking). Embedding runs on its own worker thread with a queue.

**git/** — Thin C++ wrapper over libgit2: repository discovery per path, per-file log, blob retrieval, diff, blame, staging, commit, push. All calls run off the UI thread and return futures.

**sync/** — `SyncProvider` interface (SPEC.md §Sync). `RcloneProvider` shells out to `rclone bisync`. `DriveProvider` (Phase 5) talks to the Google Drive API with a change log and merge engine. Providers see encrypted bytes when encryption is enabled. Providers operate on root 0 only; linked roots are assumed to sync through Git.

**mcp/** — `coeus --mcp` entry point. Serves the tools in SPEC.md §MCP surface over stdio against the same index. Read tools open the index read-only and work without the GUI. Write tools go through `vault/AtomicWriter` and `registers/NextId` exactly as the UI does, and, when the GUI is running, through a local socket to raise the confirmation toast.

**vault/Roots** — reads `[[roots]]` from `config.toml`, assigns `root_id`s, owns one `VaultWatcher` per present root, and marks absent roots without deleting their rows (D-010).

**search/Sidecar** — exports and imports `.coeus-shared/embeddings.bin` (SPEC.md §Embeddings sidecar) so a second machine inherits vectors by content hash instead of recomputing (D-011).

**Mnemosyne** — External subsystem providing local embeddings. Coeus links against it or talks to it over a local socket (open under D-008).

## Key invariants

1. **Vault is truth, index is cache.** Deleting `.coeus/index.db` and restarting must reproduce an identical index with no user-visible change beyond a rebuild delay. (AV-001)
2. **Writes are explicit and atomic.** Only `AtomicWriter` touches vault files, and only on behalf of a user action. (AV-002)
3. **UI thread never blocks.** Indexing, embedding, Git and sync run on worker threads; the UI observes signals. (AV-005)
4. **Register IDs are append-only.** `NextId` reads the highest existing ID and adds one; nothing renumbers. (AV-003)
5. **One vector space per index.** The embedding model name and dimension are stored in the index; a mismatch triggers a full re-embed, never a mixed table. (AV-007)
6. **Parser is pure.** Same input text always yields the same structures; no filesystem or clock access inside `parser/`.
7. **`.coeus/` never leaves the machine; `.coeus-shared/` holds only hash-keyed derived data.** The index is rebuilt per machine; only the embeddings sidecar syncs (D-011).
8. **Every write path is the same write path.** UI save, template insertion, idea promotion and MCP write tools all go through `AtomicWriter`, and every ID through `NextId`. No module writes a vault file any other way (AV-012).

## Cross-cutting concerns

**Threading.** One indexer worker, one embedding worker, one Git worker, one sync worker, each a `QThread` with a queue. Communication is signal/slot with queued connections. SQLite is opened per thread with WAL mode; the UI thread holds read-only connections.

**Error handling.** Exceptions do not cross module boundaries. Modules return `std::expected<T, Error>` (C++23, D-013); the UI maps errors to status-bar messages or dialogs. A failed index update for one file never aborts the batch.

**Logging.** `QLoggingCategory` per module (`coeus.vault`, `coeus.index`, …). Default level warning; `--verbose` raises to debug. Logs go to stderr and a rotating file under `.coeus/logs/`.

**Settings.** `QSettings` in INI format at the XDG config path for app-level settings; per-vault settings in `.coeus/config.toml`.

**Testing.** Parser and registers are covered by unit tests with no Qt event loop. Index and watcher tests run against a temporary vault fixture. UI tests are limited to smoke tests until Phase 4.
