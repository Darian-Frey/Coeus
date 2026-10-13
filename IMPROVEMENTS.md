# Improvements

Catalogue of code-quality improvements, refactors, and architectural
changes proposed during development. Per Maintenance Rule 8, improvements
are logged here when noticed, not silently applied. The author decides
whether to apply, defer, or decline.

This is the dual of BUGS.md: bugs are broken; improvements work but
could be better.

Status vocabulary: suggested | applied | declined | deferred.
Effort vocabulary: trivial | small | medium | large.

## Suggested

### IMP-003: Accept register fields written as list items
**Status:** suggested
**Found:** 2026-10-13 (F-016, linting `~/Projects`)
**Location:** `src/parser/RegisterParser.cpp`, SPEC.md §Entry grammar
**Effort:** small
**Description.** Pontus writes its IMPROVEMENTS fields as list items (`- **Status:** suggested`, `- **Trade-offs.** …`). The grammar wants a field at the start of the line, so those entries have no status in the register panel and fifteen of them are reported as missing Trade-offs (L6) although they have them.
**Proposal.** Either accept an optional `- ` before a field, or report a register with entries but no recognised fields as a lint warning so the author can conform.
**Trade-offs.** Accepting `- **Name.**` would turn bolded list items inside a paragraph field (`**Options.**` followed by `- **A. Database.** Rejected.`) into fields wherever the item's label has no inner full stop, cutting the paragraph short. The standard's own skeletons put fields at the line start; conforming Pontus is a one-off edit.
**Notes.** The parser follows the standard (AV-008); this is a question of how forgiving to be.

## Applied

### IMP-004: A `coeus --lint` command line to replace `check_xrefs.py`
**Status:** applied
**Found:** 2026-10-13 (F-016)
**Location:** `src/app/main.cpp`, `tools/check_xrefs.py`, `tools/hooks/pre-commit`
**Effort:** small
**Description.** The register lint now runs inside the app, and its L4 gives exactly the script's one-way references on Coeus's own registers. The pre-commit hook and the `check_xrefs` ctest still run the Python script, because the app has no way to lint from a terminal. Two implementations of one rule will drift.
**Proposal.** `coeus --lint <folder>` indexes the folder into a temporary index, prints problems as `path:line: level: message`, and exits 1 on errors (or warnings with `--strict`), as the script does; the hook and ctest switch to it and the script retires.
**Trade-offs.** The hook then needs a built binary, so a fresh clone cannot commit until it builds; the script needs only Python. The script also checks every Markdown document in `docs/`, not just register files, for cited IDs; the app checks Related fields only (SPEC.md §Lint rules), so broken IDs in prose would go unreported.
**Notes.** Completes what IMP-001 anticipated.
**Applied.** 2026-10-13. `registers/LintCommand` behind `coeus --lint [--strict] [--quiet] [--mentions] <folder>`, run without a display. On the second trade-off the author chose a flag over a lint rule: `--mentions` reports IDs mentioned anywhere in a project's notes that it does not define, so the hook and the `lint_coeus_docs` ctest keep the script's broader check while the Problems panel stays quiet about other projects' IDs. It reports the script's 68 one-way references on Coeus, and its tests port the script's. `check_xrefs.py` and `test_check_xrefs.py` are deleted; the hook needs `build/coeus` or `$COEUS_BIN`. Tested by `test_lint_command`.

### IMP-002: Sweep stale temporary files left by interrupted writes
**Status:** applied
**Found:** 2026-10-12 (building `AtomicWriter`)
**Location:** `src/vault/TempSweeper.cpp`, `src/ui/MainWindow.cpp` (`openVault`)
**Effort:** small
**Description.** When Coeus is killed between creating `.<name>.coeus-tmp-<random>` and renaming it, the temporary file stays in the note's folder. The original note is intact (AV-002), the explorer hides dot-files and sync excludes `*.coeus-tmp-*`, but the files accumulate and Git reports them as untracked. The kill-during-write test left one in 33 of 40 runs.
**Proposal.** On vault and root open, a worker thread deletes `*.coeus-tmp-*` files older than one minute, so a save in progress in another Coeus process (`coeus --mcp`) is never touched. Optionally include `*.coeus-tmp-*` in the first-open `.gitignore` offer.
**Trade-offs.** A walk of every folder on open; it can share the indexer's initial scan rather than add one. Deleting files is a vault write outside a user command, so CLAUDE.md §Invariants would need to name it, limited strictly to Coeus's own temporary files.
**Notes.** Related to AV-002 and BUG-003.
**Applied.** 2026-10-12. `TempSweeper::sweep` runs through `QtConcurrent` from `MainWindow::openVault` and reports a count in the status bar. It removes only regular files matching `AtomicWriter`'s name pattern, older than 60 s, outside `.git/`; symlinks are never followed or removed. CLAUDE.md §Invariants, ARCHITECTURE invariant 2 and SPEC.md §Atomic write name the exception, and the `.gitignore` offer now includes `.*.coeus-tmp-*`. Linked roots (F-034) will be swept by the same call when they are opened. Tested by `test_temp_sweeper` and `test_main_window`.

### IMP-001: Cross-reference checker for the scaffold docs
**Status:** applied
**Found:** 2026-09-25 (scaffolding session)
**Location:** `tools/check_xrefs.py`, `tools/test_check_xrefs.py`, `tools/hooks/pre-commit`
**Effort:** small
**Description.** The documentation standard's Maintenance Rule 6 requires bidirectional cross-references between F-/D-/AV-/BUG-/IMP- entries, and acknowledges the rule decays without tooling. Coeus's own docs already have several one-way references (for example AV entries cite D entries but the D entries do not all cite back).
**Proposal.** A Python script walking the six register files, building the reference graph, and reporting broken and one-way links; wired as a pre-commit hook. Once F-016 exists, the script becomes redundant and can be replaced by `coeus --lint`.
**Trade-offs.** Throwaway tooling that F-016 supersedes; but F-016 is Phase 3 and the docs need checking from Phase 1. Cost is an hour.
**Notes.** Same rule the register lint (SPEC.md §Lint L4) will enforce inside the app. F-016 now enforces it in the app with the same findings; the script was retired for `coeus --lint` by IMP-004 (2026-10-13).
**Applied.** 2026-10-12. Checks every root-level and `docs/` Markdown file. Broken and duplicate IDs are errors and fail the hook and the `check_xrefs` ctest; one-way references are listed as warnings (65 on first run, 49 of them D→F) and fail only with `--strict`. Back-reference pairs follow Maintenance Rule 6 and rule 4 of the BUGS and IMPROVEMENTS sections. The hook is enabled per clone with `git config core.hooksPath tools/hooks`.


## Declined

## Deferred
