# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8,
bugs are logged here when found, not silently fixed. The author decides
whether to fix immediately, defer, or leave alone.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

## Open

None at present.

## Fixed

### BUG-010: A status message is drawn over the note's path
**Status:** fixed (2026-10-17)
**Severity:** low
**Found:** 2026-10-15 (screenshots of F-027's themes on the demo vault, in every theme)
**Location:** `src/ui/MainWindow.cpp` (`createStatusBar`, `m_noteLabel`)
**Description.** With a note open in a vault that has a missing linked root, the bottom-left of the status bar showed the "Linked root … is missing" message and the note's path on top of each other, both unreadable. `QStatusBar` hides its normal widgets while a message shows, but clears their explicitly-hidden flag as it does. When a permanent widget of another height then comes or goes (the changed-files count, the problems button, the embedding progress, all of which appear just after a vault opens), the bar rebuilds its layout in `reformat()`, and `QLayout::addChildWidget` queues `_q_showIfNotHidden` for every widget not explicitly hidden, so the path came back over the message. Found by catching the label's Show event in gdb and reading Qt 6.4.2's `qstatusbar.cpp` and `qlayout.cpp`. Not theme-related: the themes' screenshots were simply taken just after a vault opened.
**Fix.** The window hides the path itself, explicitly, whenever `QStatusBar::messageChanged` reports a message, and shows it when the message clears; Qt's queued showing leaves explicitly hidden widgets alone. `test_main_window` shows a message, adds a taller permanent widget and checks the path stays hidden, failing before the fix.

### BUG-012: Recent notes were written only when the window closed
**Status:** fixed (2026-10-16)
**Severity:** low
**Found:** 2026-10-16 (moving recent notes into the vault session, F-012)
**Location:** `src/ui/MainWindow.cpp` (`saveRecent`), now `src/ui/VaultSession.cpp`
**Description.** Recent notes are meant to be written two seconds after they settle (SPEC.md §Bookmarks and recent notes). The timer's handler wrote them only if the timer was still active, but a single-shot `QTimer` has already stopped when it emits `timeout()`, so the handler never wrote; they were saved only on closing the window or switching vault, and lost if Coeus was killed.
**Fix.** A pending flag replaces the timer test. `test_bookmarks_window` waits for `recent.json` with the window still open.

### BUG-011: Light theme left the docks dark after Cassette
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-15 (the author, trying the themes on `~/Projects`)
**Fixed:** 2026-10-15
**Location:** `src/ui/Theme.cpp`
**Description.** After Cassette, switching to Light left the item views (Explorer, Tags, Search, Registers, Problems, the sidebar's lists) and the Search box dark, and labels and dock titles pale grey on light grey, nearly unreadable. Once an application style sheet has been applied and removed, Qt 6.4 leaves some widgets no longer following the application's palette: a later theme reached the window but not, for instance, the sidebar dock and everything in it. Cassette is the only theme with a sheet of its own; a style snippet removed would have done the same.
**Fix.** Every theme change, and every change of style sheet, lets each widget inherit the application's palette again (no Coeus widget sets one of its own). `test_theme_shortcuts::everyWidgetFollowsEveryTheme` goes Dark, Light, Cassette, Light, Dark, System and requires every widget in a window to have the application's colours in its state (enabled or disabled), with a desktop-style palette for item views planted first; without the fix it fails at the second Dark on the sidebar.

### BUG-009: Changing the theme widened the window past the screen
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-15 (the author, trying F-027 on `~/Projects`)
**Fixed:** 2026-10-15
**Location:** `src/ui/RegisterPanel.cpp`, `src/ui/NewNoteDialog.cpp`
**Description.** The Registers panel's project and status filters kept Qt's default size policy, which measures a combo box's longest item once, when it is first shown. That was before the catalogue loaded, when the status filter held only "All statuses". A restyle measures again, and the standard's statuses run to a paragraph ("Complete (Phase 3, 2026-10-14): read tools working with…"), so switching theme on the author's vault gave the box a minimum width of 2,255 px and the window grew to 2,824 px to hold it. Latent since F-015; F-027's style change set it off, as a font change would have.
**Fix.** Combo boxes filled from the vault are sized to a few words (`AdjustToMinimumContentsLengthWithIcon`), longer items elided with their full text as a tooltip. `test_theme_shortcuts::restylingNeverWidensTheWindow` switches through the three themes with such a status and requires the window to keep its width; without the fix it grows to 2,250 px.

### BUG-008: The embeddings sidecar counted as a change in Git vaults
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-15 (F-036 and F-037, `test_commit_dialog` failing on a machine with the model)
**Fixed:** 2026-10-15
**Location:** `src/git/GitRepository.cpp` (`isCoeusFile`)
**Description.** In a vault that is a Git repository, the embedding worker's `.coeus-shared/embeddings.bin` (F-035) was an untracked file like any other: the status bar's changed-files count went up after the first embedding pass and again after every edit was embedded, and the commit dialog listed it each time. Introduced with F-035 (773ef39); its tests passed because `test_commit_dialog` usually finished before the worker wrote the sidecar, and the suite runs without the model elsewhere.
**Fix.** `.coeus-shared/` joins `.coeus/` as Coeus's own: never counted, listed or committed by Coeus. It still travels by the sync provider; committing it by hand is the author's choice. `test_git_commit` gains the sidecar in status and as a refused commit; both fail with the old code.

### BUG-007: A root whose own README is a project could not be indexed
**Status:** fixed
**Severity:** high
**Found:** 2026-10-13 (F-016, linting Coeus's own registers in a test)
**Fixed:** 2026-10-13
**Location:** `src/index/ProjectResolver.cpp` (`folderOf`), `src/registers/NextId.cpp`
**Description.** A project at a root's top level has the folder "" — a linked repository whose README carries the status header (F-034), or a vault that is itself one. `folderOf` returned a null `QString` there, which binds as SQL NULL, so the project insert broke `projects.path NOT NULL` and the root's whole scan failed: none of its notes were indexed. The demo vault's Pontus root was one. Introduced with F-015 step 1 (fdfb1b3); the screenshot check then opened the demo's parent folder, where Pontus was a subfolder, so it was missed. `NextId` would likewise have refused such a project, given the null path a query returns for "".
**Fix.** `folderOf` returns `""` (in `ProjectResolver` and `LintResolver`), and `NextId` binds a null project path as `""`. Tested by `test_lint` (Coeus's own README makes its root a project) and `test_next_id::aRootThatIsItselfAProject`; both fail with the old code.

### BUG-006: Field names with a hyphen were not parsed
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-13 (F-016, surveying real registers before writing L6)
**Fixed:** 2026-10-13
**Location:** `src/parser/RegisterParser.cpp`, SPEC.md §Entry grammar
**Description.** The field pattern `[A-Za-z ]+` from SPEC excluded the hyphen, so the standard's own `**Trade-offs:**` was never a field: it was dropped, or swallowed by the field before it. Across `~/Projects`, 66 of 68 IMP entries carry it; L6 would have flagged nearly all of them.
**Fix.** Names are `[A-Za-z][A-Za-z -]*` in the parser and SPEC. `test_register_parser` parses a `Trade-offs` field; `test_lint` lints Coeus's own registers and requires no errors (AV-008).

### BUG-001: `std::expected` used under C++20
**Status:** fixed
**Severity:** high
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** ARCHITECTURE.md §Cross-cutting concerns, SPEC.md §Sync, DECISIONS.md D-002
**Description.** The error-handling convention and `SyncProvider` rely on `std::expected`, a C++23 feature, while the project was specified as C++20. The first module boundary would not have compiled.
**Fix.** Moved to C++23 (D-013); `tl::expected` fallback removed from ARCHITECTURE.

### BUG-002: Contentless FTS5 table cannot return snippets
**Status:** fixed
**Severity:** medium
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** SPEC.md §Index schema, §MCP surface
**Description.** `fts` was declared with `content=''`. Contentless FTS5 tables cannot serve `snippet()` or `highlight()`, so the search panel and the MCP `search` tool's `snippet` field were unimplementable, and row deletion needs the original values.
**Fix.** `fts` now stores its own copy of the text, keyed by `rowid = notes.id`; the incremental update deletes and re-inserts it explicitly.

### BUG-003: `.gitignore` written without a user command
**Status:** fixed
**Severity:** low
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** SPEC.md §Vault layout, DECISIONS.md D-005, CLAUDE.md §Invariants
**Description.** SPEC said `.coeus/` is added to `.gitignore` on first open, a vault write outside the editor save path and outside any explicit user command.
**Fix.** Coeus now offers the change on first open; accepting is an explicit command through `AtomicWriter`, declining is remembered. The vault layout diagram also gains `ideas/`, `.coeus/journal/` and `.coeus-shared/`. IMP-002 later added `.*.coeus-tmp-*` to the same offer.

### BUG-004: `next_id` reservations had no cross-process home
**Status:** fixed
**Severity:** high
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** SPEC.md §ID allocation, §Index schema, §MCP surface
**Description.** The MCP `next_id` tool "reserves for 60 s", but `NextId` was defined only as max+1 over the index, with nowhere to keep reservations visible to both the GUI and `coeus --mcp`. Concurrent allocation could hand out the same ID (AV-003, AV-012).
**Fix.** A `reservations` table in `index.db`; `NextId` runs under `BEGIN IMMEDIATE`, reads existing IDs from the register file on disk, and writes are re-checked against the file immediately before the atomic write.

### BUG-005: Phase 1 scope inconsistent across documents
**Status:** fixed
**Severity:** low
**Found:** 2026-10-12 (documentation review)
**Fixed:** 2026-10-12
**Location:** CLAUDE.md §Active task, ROADMAP.md Phase 1, FEATURES.md F-001, F-002
**Description.** CLAUDE.md's active task omitted the command palette that F-001 and ROADMAP require, and CLAUDE.md and ROADMAP filtered the explorer to Markdown only while F-002 includes attachment types.
**Fix.** CLAUDE.md and ROADMAP aligned with F-001 and F-002.

## Won't Fix

## Deferred
