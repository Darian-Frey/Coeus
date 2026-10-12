# Decisions

Append-only log of significant design decisions.
Each entry: D-NNN, with Decided and Recorded dates (ISO 8601; equal for normal entries, divergent for retroactive ones), status, context, alternatives, decision, consequences, and reversal conditions.
Status vocabulary: Proposed | Accepted | Superseded by D-NNN | Deprecated.

### D-001 Build a bespoke tool rather than adopt Obsidian
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-03)
**Related:** F-015, F-018, F-020

**Context.** The author wanted an Obsidian-style knowledge base over personal notes and a dozen project repositories documented to the project-scaffold standard.

**Options.**
- **A. Use Obsidian directly over the repos.** Zero build cost. Rejected: no native understanding of the scaffold registers, semantic search requires a third-party plugin plus API key, Git integration is a wrapper plugin, and the Electron runtime is heavy for the author's workflow.
- **B. Obsidian plus a custom plugin or external indexer.** Reuses the editor. Rejected: plugin API is JavaScript, sandboxed, and the differentiating features (registers, local embeddings, libgit2) would fight the host.
- **C. Bespoke tool.** Chosen. Full control over the three differentiators; reuses the author's C++/Qt experience.

**Decision.** Option C. Build Coeus as a standalone application.

**Consequences.**
- Obsidian's core takes roughly a year of solo effort to match; the plugin ecosystem is never matched. Scope is "Obsidian's core plus the handful of plugins the author actually uses, built in" (FEATURES.md §Out of scope).
- The vault must stay compatible with Obsidian's Markdown dialect so the author can fall back at any time.

**Reversal conditions.** Revisit if Obsidian ships native register-style typed objects, local embedding search, and libgit2-grade Git, or if Phase 2 has not delivered a daily-usable editor within six months of starting.

### D-002 C++20 and Qt6 for the whole application
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-03)
**Related:** F-001, F-003, F-012

**Context.** Stack choice for a Linux-only desktop tool where the Markdown editor is the largest single cost and the author's strongest experience is C++/Qt (Pontus GUI, Argo).

**Options.**
- **A. Rust + Qt6 via cxx-qt.** Memory safety; FFI friction on every widget; Qt docs assume C++.
- **B. Rust + egui.** Pure Rust, fast iteration; no serious editor widget, weak docking and file trees.
- **C. Tauri with a web frontend.** CodeMirror 6 solves the editor for free; WebKitGTK dependency and a second language for the UI.
- **D. C++20 + Qt6.** Chosen. No FFI, mature docking/tree/text widgets, author's deepest experience.

**Decision.** Option D.

**Consequences.**
- Editor is built on `QPlainTextEdit`/`QTextDocument`; live preview (F-003) is custom work rather than a drop-in.
- Memory safety relies on discipline: RAII, Qt parent ownership, `std::unique_ptr`, sanitizers in CI.

**Reversal conditions.** Revisit if the live-preview editor cannot reach acceptable quality within Phase 4, in which case embedding a web editor via `QWebEngineView` for the editor pane only is the fallback.

### D-003 Linux desktop only
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** D-002, F-028

**Context.** Mobile support changes the stack more than any other requirement.

**Options.**
- **A. Linux + Android.** Would force Tauri/Flutter or a web UI. Rejected: no stated mobile need.
- **B. Linux desktop only.** Chosen.

**Decision.** Option B. No mobile, no Windows/macOS targets in scope.

**Consequences.**
- Frees the stack choice (D-002).
- Sync (F-028, F-029) exists for a second Linux machine, not a phone.

**Reversal conditions.** Revisit if a mobile requirement appears; the vault format and index schema are platform-neutral so a separate mobile client remains possible.

### D-004 GUI shell first; defer live preview
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-03)
**Related:** F-001, F-003, ROADMAP.md Phase 1

**Context.** The author wants to test features as they are added rather than build a backend blind.

**Options.**
- **A. Index and parser first, headless.** Testable via unit tests but nothing to look at for weeks.
- **B. Shell first.** Chosen. Every later feature is a panel or command in an existing window.

**Decision.** Option B. Phase 1 is the `QMainWindow` skeleton with docks, tabs, toolbar, status bar and command palette. Phase 2 ships source and reading modes; live preview moves to Phase 4.

**Consequences.**
- Early visible progress; UI conventions settle before content arrives.
- Live preview, the costliest editor feature, is built against a stable shell rather than in parallel with it.

**Reversal conditions.** Revisit if the shell proves to need redesign once real panels arrive; at that point a headless core with a rewritten shell is acceptable.

### D-005 Vault is a plain folder; Coeus keeps only a rebuildable index
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-002, F-011, AV-001, AV-002

**Context.** The author's notes already live in Git repositories and are edited by other tools (Claude Code, Vim). Coeus must not become the only way to read them.

**Options.**
- **A. Proprietary database with Markdown export.** Rejected: lock-in, breaks Git and every other editor.
- **B. Plain folder of `.md` plus a derived SQLite index in `.coeus/`.** Chosen.

**Decision.** Option B. The index (`.coeus/index.db`) is a cache; deleting it loses nothing.

**Consequences.**
- All state that matters must round-trip through Markdown and frontmatter (or JSON files for Canvas).
- A file watcher (F-011) is mandatory, not optional.
- `.coeus/` is added to the vault's `.gitignore` by default.

**Reversal conditions.** Revisit only if a feature genuinely cannot be represented in plain files; Canvas and Bases (F-030, F-031) are expected to fit as JSON.

### D-006 Three defining differentiators
**Decided:** 2026-09-25
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-25)
**Related:** F-015, F-016, F-017, F-018, F-019, F-020, F-021, F-022

**Context.** With Obsidian's plugin ecosystem unmatchable, Coeus needs features that make it obviously the author's tool rather than a lesser clone.

**Options.** Candidates included scaffold-aware registers, Mnemosyne semantic search with a related-notes sidebar, native Git, a review-freshness dashboard, Crucible mode, LLM hooks, Aporia integration, Subtle Chaos lint, native multi-window, vault snapshots.

**Decision.** Scaffold-aware registers, Mnemosyne semantic search with related-notes sidebar, and native Git are the three Phase 3 differentiators. The review-freshness dashboard rides along as F-017 because it reuses the register parser. The rest are candidate features in FEATURES.md.

**Consequences.**
- Phase 3 is the product's reason to exist; Phases 4–6 are parity work.
- Mnemosyne becomes a hard dependency (D-008).

**Reversal conditions.** Revisit if any of the three proves unusable in daily work after Phase 3, or if a candidate feature turns out to be more valuable in practice.

### D-007 Sync through a provider interface; rclone first, Google Drive native later
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-03)
**Related:** F-028, F-029, AV-004

**Context.** The author wants sync configurable to use Google Drive without paying for a hosted service.

**Options.**
- **A. Folder sync only (rclone / Insync), no code in Coeus.** Cheapest; weak conflict handling and no version history.
- **B. Native Drive API sync built in from the start.** Proper conflicts and history; a quarter of the project's effort before anything else works.
- **C. Provider interface; ship rclone provider first, native Drive provider in Phase 5.** Chosen.

**Decision.** Option C.

**Consequences.**
- `SyncProvider` is designed in SPEC.md before Phase 2 so the index and watcher don't assume single-machine use.
- Encryption is a provider-layer concern; Drive holds ciphertext when enabled.

**Reversal conditions.** Revisit if rclone bisync proves reliable enough that native Drive sync adds no value, in which case F-029 is Withdrawn.

### D-008 Embedding model and Mnemosyne integration mechanism
**Decided:** —
**Recorded:** 2026-09-25
**Status:** Proposed
**Authors:** Shane Hartley
**Related:** F-018, F-019, AV-007, SPEC.md §Embeddings

**Context.** Semantic search needs a local embedding model and a way for Coeus to call Mnemosyne.

**Options.**
- **A. Link Mnemosyne as a library.** Tight coupling, single process, simplest deployment.
- **B. Mnemosyne as a local service over a Unix socket.** Shared with other tools (Aporia, tabletop bot); one model instance in memory.
- **C. Embed directly with llama.cpp or ONNX Runtime inside Coeus, bypassing Mnemosyne.** No shared benefit.

Model candidates are to be evaluated for quality on the author's own notes, memory footprint on 32–64 GB RAM with a 4 GB VRAM GPU, and dimension (stored in the index per AV-007).

**Decision.** Pending. To be decided at the start of Phase 3 after a benchmark on the author's vault.

**Consequences.** Until decided, SPEC.md §Embeddings records the interface only; the index stores model name and dimension so the choice can change with a re-embed.

**Reversal conditions.** N/A until Accepted.

### D-009 License
**Decided:** —
**Recorded:** 2026-09-25
**Status:** Proposed
**Authors:** Shane Hartley
**Related:** README.md §License

**Context.** Tier 1 requires a license before the first public commit. Qt6 is available under LGPL v3 (dynamic linking) or GPL v3; libgit2 is GPL v2 with linking exception; sqlite-vec is Apache 2.0 / MIT.

**Options.**
- **A. GPL v3.** Compatible with all dependencies including static Qt.
- **B. MIT or Apache 2.0.** Requires dynamic linking to Qt under LGPL; otherwise fine.

**Decision.** Pending. Author to choose before the first push.

**Consequences.** Until chosen, the repository stays private.

**Reversal conditions.** N/A until Accepted.

### D-010 Linked project roots outside the vault folder
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-002, F-011, F-020, F-032, D-005, D-007, D-011, AV-010, SPEC.md §Linked roots

**Context.** The author's project repositories live in their own directories under Git, while personal notes live in a vault folder that will sync through Google Drive. Coeus must index both as one knowledge base, and the project repos must not pass through Drive (`.git` directories corrupt under file sync).

**Options.**
- **A. Require all repos to live inside the vault folder.** Simplest indexer. Rejected: forces repos through Drive sync, or forces the vault out of Drive.
- **B. Symlinks from the vault into the repos.** Works on one machine; breaks on the second machine where paths differ, and Drive sync follows or drops symlinks unpredictably.
- **C. Linked project roots.** Chosen. Per-vault config lists external directories; each is indexed as a root alongside the vault with its own watcher, and excluded from vault sync.

**Decision.** Option C. `.coeus/config.toml` gains a `[[roots]]` list. Every indexed note carries a `root_id`. Paths in the index are root-relative; the UI shows them as `<root name>/<path>`.

**Consequences.**
- Wikilink resolution (SPEC.md) gains a root dimension: same-root matches rank above cross-root.
- Root paths are machine-specific; the config is per-machine and not synced. A root missing on a machine is shown greyed, not deleted from the index.
- The sync layer (D-007) operates on the vault root only; linked roots are assumed to have their own sync (Git).

**Reversal conditions.** Revisit if multi-root indexing makes the watcher or link resolution unacceptably complex, in which case Option A with a documented "repos vault" is the fallback.

### D-011 Sync the vault, never the index; embeddings travel as a sidecar
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-018, F-028, F-029, D-005, D-007, D-010, AV-004, AV-007, AV-011, SPEC.md §Embeddings sidecar

**Context.** The author wants one copy of Coeus's data on the desktop, one on the laptop, kept in step through Google Drive.

**Options.**
- **A. Sync `.coeus/index.db` through Drive.** Rejected: SQLite in WAL mode does not survive mid-write copies; two machines would overwrite each other; the file holds machine-specific root paths.
- **B. Sync only the Markdown vault; each machine rebuilds its own index.** Correct by D-005, but re-embedding a thousand notes on the laptop (no usable GPU) is slow.
- **C. Option B plus an embeddings sidecar.** Chosen. The vault carries `.coeus-shared/embeddings.bin`, an export of chunk hashes and vectors. A machine imports matching hashes and embeds only what has changed.

**Decision.** Option C. `.coeus/` is never synced. `.coeus-shared/` is synced and holds only derived data that is expensive to recompute and safe to merge by content hash.

**Consequences.**
- Both machines show the same vault after a sync; full-text index rebuilds in seconds.
- The sidecar is append-and-compact: imports union by hash; a periodic compaction drops hashes no longer in the vault.
- Sidecar is tied to `embed_model`/`embed_dim`; a mismatch is ignored, not imported (AV-007).

**Reversal conditions.** Revisit if the sidecar proves conflict-prone under Drive sync, in which case embeddings are rebuilt per machine and the laptop accepts the cost.

### D-012 Coeus exposes its index to AI sessions over MCP
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-015, F-018, F-033, D-006, AV-012, SPEC.md §MCP surface

**Context.** Project ideas are developed in Claude chat sessions and handed to Claude Code in the Antigravity IDE. The author wants both to read from and write into Coeus without manual file copying.

**Options.**
- **A. Manual: download scaffold docs from chat and drop them in the vault.** Works today; no code. Remains the fallback.
- **B. Rely on the linked-computer bridge to write files into the vault.** Works for chat, not for Claude Code, and bypasses register ID allocation.
- **C. Coeus runs a local MCP server exposing search, related notes, register queries, next-ID allocation, and entry appends.** Chosen. One index serves every AI session; writes go through the same `AtomicWriter` and `NextId` paths as the UI.

**Decision.** Option C. A `coeus-mcp` binary (or `coeus --mcp`) speaking MCP over stdio, backed by the same index. Write tools are opt-in per vault and confirm in the UI when Coeus is running.

**Consequences.**
- Claude Code in Antigravity and Claude chat can log decisions, bugs and improvements straight into the right register file with the right ID.
- Mnemosyne-backed semantic search becomes available inside any AI session.
- A new attack surface: writes from an AI session must obey every invariant (AV-012).

**Reversal conditions.** Revisit if MCP is superseded as the tool protocol the author's AI tools speak, or if write tools cause register corruption that the confirmation step does not prevent.
