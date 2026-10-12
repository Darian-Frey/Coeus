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

### IMP-001: Cross-reference checker for the scaffold docs
**Status:** suggested
**Found:** 2026-09-25 (scaffolding session)
**Location:** `tools/check_xrefs.py` (does not exist)
**Effort:** small
**Description.** The documentation standard's Maintenance Rule 6 requires bidirectional cross-references between F-/D-/AV-/BUG-/IMP- entries, and acknowledges the rule decays without tooling. Coeus's own docs already have several one-way references (for example AV entries cite D entries but the D entries do not all cite back).
**Proposal.** A Python script walking the six register files, building the reference graph, and reporting broken and one-way links; wired as a pre-commit hook. Once F-016 exists, the script becomes redundant and can be replaced by `coeus --lint`.
**Trade-offs.** Throwaway tooling that F-016 supersedes; but F-016 is Phase 3 and the docs need checking from Phase 1. Cost is an hour.
**Notes.** Same rule the register lint (SPEC.md §Lint L4) will enforce inside the app.

## Applied

## Declined

## Deferred
