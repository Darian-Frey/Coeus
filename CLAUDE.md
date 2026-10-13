# CLAUDE.md

## Project
Coeus is a local-first Markdown knowledge base for Linux desktop in C++23 and Qt6. The vault is a plain folder of `.md` files plus linked project repositories (F-034); Coeus keeps only a rebuildable SQLite index beside it. Three features define the product: scaffold-aware registers and the project board (F-015, F-016, F-032), Mnemosyne semantic search with a related-notes sidebar (F-018, F-019), and native Git (F-020–F-022). An MCP server (F-033) lets Claude chat and Claude Code read and write the same index.

## Current state
- Phase 1 shell builds and passes its tests (see BUILD.md): `src/app/main.cpp`, `src/vault/{Vault,AtomicWriter,TempSweeper}`, `src/parser/{NoteParser,Frontmatter,HtmlRenderer,RegisterParser}`, `src/registers/{Vocabulary,NextId,RegisterTemplates,Lint}`, `src/index/{Database,Schema,IndexStore,ProjectResolver,LintResolver,IndexQueries,Backlinks,Tags,Search,SearchQuery,LinkResolver,Indexer,IndexService,ContentHash}`, `src/vault/{Roots,GitIgnore,GitIgnoreOffer,VaultConfig,VaultWatcher,Journal,DailyNotes,Templates}`, `src/ui/{MainWindow,NoteView,Editor,PropertiesPanel,BacklinksPanel,OutlinePanel,TagPanel,SearchPanel,WikilinkCompleter,WikilinkCompletion,NoteCatalogue,RegisterPanel,ProblemsPanel,MarkdownHighlighter,ExplorerPanel,SidebarPanel,CommandPalette,FuzzyMatch}`; tests under `tests/{vault,parser,index,ui,perf}`.
- Remaining `src/` module folders are empty placeholders. `tools/` holds the pre-commit hook (`coeus --lint`, Maintenance Rule 6) and `make_demo_vault.py`, which builds a vault exercising every feature (BUILD.md §Demo vault).
- UI mockup of the target main window: `docs/design/main-window.png`.
- `BUILD.md`: written 2026-10-12 from the first successful build.
- `LICENSE`: MIT (D-009). Qt6 must stay dynamically linked.

## Active task
**Phase 3 — Differentiators** (see ROADMAP.md). Order agreed 2026-10-13: registers (F-015, F-016) → native Git (F-020–F-022) → project board (F-032) → MCP server (F-033) → semantic search (F-018, F-019, F-035). D-008 (embedding model, Mnemosyne integration) stays Proposed until the author benchmarks models on their notes; Mnemosyne does not exist yet.

Registers are complete (F-015, F-016, 2026-10-13): parser, projects, register panel, ID completion, Insert Next ID through `NextId`, `{{next:KIND}}`, lint L1–L9 with the Problems panel and editor underlines (schema 3). Next: native Git (F-020–F-022), starting with the libgit2 wrapper in `src/git/`.

Phase 2's deliverables are all built (2026-10-13); its acceptance week of daily use is the author's to run, and bugs found then go in BUGS.md.

Phase 1 (GUI shell, F-001, F-002) completed 2026-10-12. Every later feature is a panel added to `SidebarPanel`, a dock, or a command passed to `MainWindow::registerAction()`; do not restructure the shell to add one.

## Invariants
- The vault is the source of truth. The index is derived and must be fully rebuildable from the vault with no data loss (AV-001).
- Coeus writes to a vault file only through the editor save path or an explicit user command (template insertion, daily-note creation, register ID assignment, accepting the first-open `.gitignore` offer). Never on index rebuild, sync, or Git operations. The single exception is `TempSweeper` deleting Coeus's own stale `.*.coeus-tmp-*` files on vault open (IMP-002); widen it for nothing else.
- All vault writes are atomic: write to a temp file in the same directory, fsync, rename (AV-002).
- The UI thread never blocks on indexing, embedding, Git, or sync. Those run on worker threads and report back via signals (AV-005).
- Register IDs are append-only and never renumbered (AV-003).
- Sync providers implement `SyncProvider`, operate on root 0 only, and never see plaintext when encryption is enabled.
- `.coeus/` is never synced; `.coeus-shared/` holds only hash-keyed derived data (D-011).
- Every note row carries a `root_id` from day one (D-010); do not write code that assumes a single root.
- MCP write tools use the same `AtomicWriter` and `NextId` as the UI, nothing else (AV-012).

## Build & test
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOEUS_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```
Verified 2026-10-12; details and dependencies in BUILD.md. New sources are listed explicitly in `coeus_core` in the top-level `CMakeLists.txt`; new tests use `coeus_add_test` in `tests/CMakeLists.txt`.

## Conventions
- British English in all prose, comments, and UI strings.
- ISO 8601 dates everywhere.
- Naming: `PascalCase` classes, `camelCase` methods and locals, `m_` prefix for members, `k` prefix for constants. Files match class names (`VaultWatcher.hpp` / `VaultWatcher.cpp`).
- Qt signals/slots via the function-pointer syntax; no string-based `SIGNAL()`/`SLOT()`.
- No raw `new` outside Qt parent-ownership. Non-Qt objects use `std::unique_ptr`.
- Commit messages are multi-paragraph, reference F-/D-/AV-/BUG-/IMP- IDs, and carry exactly one Subtle Chaos anomaly.
- Docs are part of the commit (Maintenance Rule 7). A change that invalidates a document without updating it is incomplete.
- Bugs and improvement candidates found during other work go in BUGS.md / IMPROVEMENTS.md, not fixed inline (Maintenance Rule 8).

## Pitfalls
- See ATTACK_VECTORS.md for the canonical list.
- `VaultWatcher` uses inotify directly, one watch per folder (D-019): `QFileSystemWatcher` directory watches miss in-place writes, and per-file watches exhaust `fs.inotify.max_user_watches`. BUILD.md has the limit guidance.
- Editors that save via rename (Vim, Claude Code) produce delete+create events, not modify. The watcher must handle both.
- `sqlite-vec` must be loaded as an extension before any `vec0` table is touched; load it in the connection factory, not per-query.
- Qt destroys a widget's C++ members before its child widgets. A child's `destroyed()` handler that touches the parent's members is a use-after-free during the parent's destructor; track children with `QPointer` instead. Found by AddressSanitizer: run the sanitizer build (BUILD.md) after UI changes.
- `QPlainTextEdit::setPlainText` clears the document's modified flag. Tests that simulate typing must insert text, or the buffer counts as clean.
- Never rebuild or delete a widget inside a slot of that widget's own signal (Qt calls it undefined behaviour); queue the work (`QMetaObject::invokeMethod(…, Qt::QueuedConnection)`) and remove widgets with `deleteLater()`, as `PropertiesPanel` does.
- Run `./build/coeus --lint --mentions .` after editing any register entry (the `lint_coeus_docs` ctest entry does too). Broken IDs are errors; one-way references are warnings, not yet all resolved, so cross-references may still be stale (Maintenance Rule 6).

## Out of scope
- Do not add a plugin system, mobile build, or Publish feature.
- Do not change the stack (C++23 + Qt6, D-002, D-013) or the vault-is-a-folder model without a DECISIONS entry.
- Do not implement native Google Drive sync before Phase 5; rclone is the interim.
- Do not enable MCP write tools by default or bypass the GUI confirmation.
- Do not renumber or delete any F-/D-/AV-/BUG-/IMP- entry.
