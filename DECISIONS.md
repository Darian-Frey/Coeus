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
**Related:** F-001, F-003, F-012, D-013, AV-005, BUG-001

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

**Amended 2026-10-12.** The language standard is C++23, not C++20 (D-013). Qt6 and the rest of this decision stand.

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
**Related:** F-001, F-003, AV-005, ROADMAP.md Phase 1

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
**Related:** F-002, F-011, AV-001, AV-002, AV-006

**Context.** The author's notes already live in Git repositories and are edited by other tools (Claude Code, Vim). Coeus must not become the only way to read them.

**Options.**
- **A. Proprietary database with Markdown export.** Rejected: lock-in, breaks Git and every other editor.
- **B. Plain folder of `.md` plus a derived SQLite index in `.coeus/`.** Chosen.

**Decision.** Option B. The index (`.coeus/index.db`) is a cache; deleting it loses nothing.

**Consequences.**
- All state that matters must round-trip through Markdown and frontmatter (or JSON files for Canvas).
- A file watcher (F-011) is mandatory, not optional.
- `.coeus/` is added to the vault's `.gitignore` by default. (Amended 2026-10-12: offered on first open rather than written unasked, to respect the write invariant; see SPEC.md §Vault layout and BUG-003.)

**Reversal conditions.** Revisit only if a feature genuinely cannot be represented in plain files; Canvas and Bases (F-030, F-031) are expected to fit as JSON.

### D-006 Three defining differentiators
**Decided:** 2026-09-25
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-25)
**Related:** F-015, F-016, F-017, F-018, F-019, F-020, F-021, F-022, AV-003, AV-008 (F-017, cited as written, was absorbed into F-032 on 2026-10-12)

**Context.** With Obsidian's plugin ecosystem unmatchable, Coeus needs features that make it obviously the author's tool rather than a lesser clone.

**Options.** Candidates included scaffold-aware registers, Mnemosyne semantic search with a related-notes sidebar, native Git, a review-freshness dashboard, Crucible mode, LLM hooks, Aporia integration, Subtle Chaos lint, native multi-window, vault snapshots.

**Decision.** Scaffold-aware registers, Mnemosyne semantic search with related-notes sidebar, and native Git are the three Phase 3 differentiators. The review-freshness dashboard rides along as F-017 because it reuses the register parser. The rest are candidate features in FEATURES.md.

**Consequences.**
- Phase 3 is the product's reason to exist; Phases 4–6 are parity work.
- Mnemosyne becomes a hard dependency (D-008).

**Reversal conditions.** Revisit if any of the three proves unusable in daily work after Phase 3, or if a candidate feature turns out to be more valuable in practice.

**Amended 2026-10-12.** F-017 is Withdrawn and absorbed into F-032; the freshness view is now a column of the project board.

### D-007 Sync through a provider interface; rclone first, Google Drive native later
**Decided:** 2026-09-03
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-09-03)
**Related:** F-028, F-029, AV-004, AV-009

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
**Decided:** 2026-10-14
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-14)
**Related:** F-018, F-019, F-035, AV-005, AV-007, D-021, SPEC.md §Embeddings

**Context.** Semantic search needs a local embedding model and a way for Coeus to call Mnemosyne.

**Options.**
- **A. Link Mnemosyne as a library.** Tight coupling, single process, simplest deployment.
- **B. Mnemosyne as a local service over a Unix socket.** Shared with other tools (Aporia, tabletop bot); one model instance in memory.
- **C. Embed directly with llama.cpp or ONNX Runtime inside Coeus, bypassing Mnemosyne.** No shared benefit.

Model candidates are to be evaluated for quality on the author's own notes, memory footprint on 32–64 GB RAM with a 4 GB VRAM GPU, and dimension (stored in the index per AV-007).

**Decision.** Option C. The model is BAAI's bge-small-en-v1.5 (MIT; 33M parameters, 384 dimensions, up to 512 tokens), chosen by the author on 2026-10-14 over all-MiniLM-L6-v2 (weaker) and nomic-embed-text-v1.5 (stronger on long notes, four times the size), and over a hosted embeddings API, which would send every note off the machine. Coeus runs it on the CPU with llama.cpp, built from source at the pinned release b10926 (MIT) as part of Coeus's own build, in `search/Embedder`. The weights are CompendiumLabs' f16 GGUF conversion (67 MB, pinned by revision and SHA-256); they are kept out of the repository, which GitHub's 100 MB file limit would refuse in full precision and which is no place for weights in any case, and `tools/fetch_model.py` downloads and verifies them once per machine. Mnemosyne (option B) does not exist; if it comes, it can stand behind the same interface.

**Consequences.**
- Semantic search works offline, on any machine Coeus builds on, with no GPU; no note leaves the machine.
- A clean build fetches and compiles llama.cpp (about 90 s on the author's machine); it is compiled optimised even in Debug builds.
- Throughput on the author's i7-11850H is about 25 passages of 170 tokens a second: a first full embedding of a vault of a few thousand chunks takes minutes on a worker thread, later saves fractions of a second.
- A machine without the model file still builds and runs; semantic features say how to fetch it, and their tests skip.
- Search queries carry bge's retrieval prefix; passages do not.

**Reversal conditions.** Revisit if retrieval on the author's notes proves poor (then a larger model, with a full re-embed as AV-007 requires), if llama.cpp's API churn makes upgrades costly (then ONNX Runtime), or if Mnemosyne becomes a shared service worth calling.

**Amended 2026-10-14.** Vectors are kept in an ordinary table, one per distinct passage keyed by its hash, and scanned in C++ for each query, rather than in `sqlite-vec` as F-018's acceptance first said: at the author's scale (3,661 passages of 384 floats) a scan takes under a millisecond, and it spares a pre-1.0 extension that would have to be built and loaded into every connection. Revisit if a vault reaches hundreds of thousands of passages.

### D-009 License
**Decided:** 2026-10-12
**Recorded:** 2026-09-25
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** D-002, D-008, README.md §Licence

**Context.** Tier 1 requires a license before the first public commit. Qt6 is available under LGPL v3 (dynamic linking) or GPL v3; libgit2 is GPL v2 with linking exception; sqlite-vec is Apache 2.0 / MIT.

**Options.**
- **A. GPL v3.** Compatible with all dependencies including static Qt.
- **B. MIT or Apache 2.0.** Requires dynamic linking to Qt under LGPL; otherwise fine.

**Decision.** Option B, MIT. `LICENSE` holds the MIT text, copyright 2026 Shane Hartley. Chosen over GPL v3 for simplicity and consistency with the author's other projects; Apache 2.0's patent grant adds little for a solo project.

**Consequences.**
- Qt6 must be linked dynamically under the LGPL v3. Distribution Qt packages on Linux already are; a static Qt build is not permitted without revisiting this decision.
- libgit2's linking exception, sqlite-vec (MIT/Apache 2.0) and SQLite (public domain) impose nothing further.
- The first commit (2026-10-12) was pushed to a public GitHub repository before this decision was recorded, contrary to the original intent that the repository stay private until a licence was chosen. The gap lasted one commit; this entry closes it.
- D-008 must account for Mnemosyne's licence: linking it as a library (option A) is only open if that licence is MIT-compatible; a socket service (option B) is unaffected.

**Reversal conditions.** Revisit if a dependency that requires GPL linkage becomes unavoidable (for example a GPL Mnemosyne linked in-process under D-008), or if the author wants forks to remain open, in which case relicensing to GPL v3 is possible while the author is the sole copyright holder.

### D-010 Linked project roots outside the vault folder
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-002, F-011, F-020, F-032, D-005, D-007, D-011, AV-006, AV-010, SPEC.md §Linked roots

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

**Amended 2026-10-14.** The sidecar is rewritten whole, atomically, as a union by hash, rather than appended to: an append in place could be copied half-written by the sync tool, and SPEC.md required atomic writes as well. It is rewritten when an embedding pass ends with something new, not after every batch. A machine's first compaction waits a full interval, so a newly set-up machine does not drop vectors of linked roots it lacks.

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

### D-013 C++23 language standard
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** D-002, BUG-001, ARCHITECTURE.md §Cross-cutting concerns, SPEC.md §Sync

**Context.** D-002 chose C++20, but ARCHITECTURE.md makes `std::expected<T, Error>` the error-handling convention for every module boundary and SPEC.md's `SyncProvider` uses it. `std::expected` is a C++23 library feature; GCC and Clang do not expose it under `-std=c++20`.

**Options.**
- **A. Stay on C++20 and vendor `tl::expected`.** Header-only, CC0. Adds a dependency and an alias to be removed later.
- **B. Move to C++23.** Chosen. The stated minimum compilers (GCC 13+, Clang 17+) already ship `std::expected` in C++23 mode; Qt 6.4+, the distribution-packaged minimum, builds under it.

**Decision.** Option B. CMake sets `CMAKE_CXX_STANDARD 23` with `CMAKE_CXX_STANDARD_REQUIRED ON` and extensions off.

**Consequences.**
- No extra dependency for error handling; SPEC and ARCHITECTURE use `std::expected` as written.
- Other C++23 features (`std::print`, deducing `this`, `std::ranges::to`) are available, but only where both minimum compilers support them; anything newer needs this entry revisiting.

**Reversal conditions.** Revisit if a target distribution's packaged compiler cannot build C++23 with `std::expected`, in which case Option A is the fallback.

### D-014 Markdown parsing with md4c
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-003, F-004, F-007, F-013, F-023, F-024, F-025, D-002, SPEC.md §Markdown dialect

**Context.** Phase 2 needs a CommonMark parser for the index (links, tags, headings, block IDs, each with a line number) and a renderer for reading mode. SPEC.md §Markdown dialect adds wikilinks, embeds, tags, block IDs, callouts, maths and highlights on top of CommonMark 0.31. ARCHITECTURE requires the parser to be pure and testable without an event loop.

**Options.**
- **A. md4c (0.4.8, packaged as `libmd4c-dev` and `libmd4c-html0-dev`, MIT).** A fast SAX-style C parser, CommonMark 0.31 compliant. It has flags for wikilinks (`[[target|label]]`), LaTeX maths spans, tables, strikethrough and task lists, and comes with an HTML renderer. Qt's own `QTextDocument::setMarkdown` is built on it, so reading-mode output matches what Qt widgets expect. It does not report node source positions; text runs arrive as pointers into the input buffer, so line numbers are computed from offsets.
- **B. cmark-gfm (0.29, packaged, BSD-2).** An AST with `sourcepos` (line and column) on every block node, plus GFM extensions. It has no wikilink or maths support, and inline source positions are unreliable in this version.
- **C. Write our own parser.** Full control over positions and extensions. Matching the CommonMark spec's edge cases is months of work and a standing source of bugs.

**Decision.** Option A. The source buffer is UTF-8 and held for the whole parse, so offset-to-line mapping is a binary search over line starts. Frontmatter is stripped before md4c sees the text. Embeds (`![[…]]`), tags, block IDs, callouts and `==highlight==`, which md4c does not know, are recognised by `parser/` in md4c's text and block callbacks.

**Consequences.**
- One parser feeds both the index and reading mode.
- `parser/` owns an extension layer above md4c, covered by `tests/parser/` fixtures for every construct in SPEC.md §Markdown dialect.
- The source-mode highlighter (`QSyntaxHighlighter`) works per line and does not use md4c; parser and highlighter must agree on the extension grammars, which SPEC.md defines.

**Reversal conditions.** Revisit if offset-based line mapping proves unreliable for constructs md4c rewrites (entities, tabs), in which case Option B for positions with our own extension pass is the fallback.

**Amended 2026-10-13.** Reading mode does not use md4c-html. It offers no extension points: wikilinks come out as `<x-wikilink>` elements QTextBrowser ignores, embeds, tags, callouts, highlights and block IDs are left as text, and patching its HTML afterwards would also rewrite those constructs inside code. `parser/HtmlRenderer` writes the HTML from md4c's own callbacks instead, the same parser the index uses, so both agree on what is code. `libmd4c-html0-dev` is no longer a dependency.

### D-015 YAML frontmatter with yaml-cpp
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-008, F-032, SPEC.md §Markdown dialect, §Ideas

**Context.** Frontmatter is YAML at byte 0 (SPEC.md §Markdown dialect). Coeus only reads it in Phase 2; the properties panel writes it back from Phase 2 on, so round-tripping must not reorder or reformat keys the author did not touch.

**Options.**
- **A. yaml-cpp (0.8, packaged as `libyaml-cpp-dev`, MIT).** Full YAML 1.2, mature. It reports errors by throwing, and its emitter does not preserve comments or formatting.
- **B. rapidyaml.** Faster and preserves more formatting. It is not packaged for Ubuntu 24.04, so it would be vendored.
- **C. Our own subset parser** for the SPEC subset (scalars, lists, flat maps). Small, but every Obsidian vault in the wild will contain YAML outside the subset.

**Decision.** Option A for reading. yaml-cpp exceptions are caught inside `parser/` and returned as `std::expected` errors, so they never cross a module boundary (ARCHITECTURE.md §Error handling). Writing a changed property edits only that key's lines in the original text rather than re-emitting the whole block, so comments and ordering survive.

**Consequences.**
- Invalid frontmatter becomes a lint entry on the note; it never blocks indexing the body.
- The line-level property writer needs its own tests.

**Reversal conditions.** Revisit if yaml-cpp's parse cost dominates a full index rebuild, in which case Option B is the fallback.

### D-016 config.toml with toml++
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-034, D-010, SPEC.md §Linked roots, §Constants

**Context.** Per-vault settings, linked roots and every constant in SPEC.md §Constants live in `.coeus/config.toml`.

**Options.**
- **A. toml++ (3.4, packaged as `libtomlplusplus-dev`, MIT).** Header-only, TOML 1.0, can run without exceptions (`TOML_EXCEPTIONS=0` returns a parse result), and preserves source positions for error messages.
- **B. toml11.** Comparable, not packaged.
- **C. Switch config to `QSettings` INI.** No dependency; loses `[[roots]]` arrays of tables and conflicts with SPEC.md.

**Decision.** Option A, built with exceptions off.

**Consequences.**
- Malformed config is reported with line and column and falls back to defaults; it never prevents the vault opening.
- Coeus rewrites `config.toml` only on an explicit settings change.

**Reversal conditions.** Revisit if toml++ is dropped from the distribution, in which case it is vendored as a single header.

### D-017 Hashing and cryptography with libsodium
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-018, F-029, F-035, AV-001, AV-009, SPEC.md §Index schema, §Embeddings sidecar, §Sync

**Context.** SPEC.md specifies BLAKE2b-128 content hashes for notes and chunks (Phase 2), and Argon2id key derivation with XChaCha20-Poly1305 encryption for sync (Phase 5).

**Options.**
- **A. libsodium (1.0.18, packaged as `libsodium-dev`, ISC).** `crypto_generichash` is BLAKE2b with a 16-byte output; the same library provides Argon2id and XChaCha20-Poly1305, and is widely audited.
- **B. libb2 now, another crypto library in Phase 5.** Two dependencies where one suffices.
- **C. `QCryptographicHash`.** Qt 6.4 offers BLAKE2b at 160, 256, 384 and 512 bits but not 128, and no encryption.

**Decision.** Option A.

**Consequences.**
- One dependency covers hashing now and encryption later; `sodium_init()` is called once in `main()`.
- Changing the hash would invalidate every `hash` column and the embeddings sidecar, so the algorithm is fixed by SPEC.md, not configurable.

**Reversal conditions.** Revisit only if libsodium becomes unmaintained.

### D-018 SQLite through the C API, not Qt SQL
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-006, F-018, D-005, AV-001, AV-005, AV-007, SPEC.md §Index schema

**Context.** The index uses SQLite with WAL, FTS5, foreign keys, a per-thread connection model (ARCHITECTURE.md §Threading), and from Phase 3 the `sqlite-vec` extension loaded in the connection factory (CLAUDE.md §Pitfalls). The system SQLite on the development machine is 3.45.1, built with `ENABLE_FTS5`, `ENABLE_LOAD_EXTENSION` and `THREADSAFE=1`.

**Options.**
- **A. Qt SQL (`QSQLITE` driver).** Familiar Qt types. Extension loading needs the raw handle anyway; connections are tied to the thread that created them through a global registry; prepared statements and transactions are less direct.
- **B. SQLite C API behind a thin RAII wrapper in `index/`.** `Database`, `Statement` and `Transaction` classes returning `std::expected`. Full control over pragmas, extension registration (`sqlite3_auto_extension` for `sqlite-vec`) and `BEGIN IMMEDIATE` for `NextId` (SPEC.md §ID allocation).
- **C. A third-party C++ wrapper (SQLiteCpp, sqlite_modern_cpp).** Saves the wrapper; adds a dependency that throws exceptions.

**Decision.** Option B, linking the system `libsqlite3`. `sqlite-vec` is vendored as its amalgamation in Phase 3, because it is not packaged.

**Consequences.**
- About 300 lines of wrapper code with its own tests.
- No Qt SQL dependency; `index/` stays usable from `coeus --mcp` without a GUI.

**Reversal conditions.** Revisit if the wrapper grows into a general ORM; at that point Option C is cheaper.

### D-019 Watch the vault with inotify directly, not QFileSystemWatcher
**Decided:** 2026-10-12
**Recorded:** 2026-10-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-12)
**Related:** F-011, F-034, D-003, D-005, AV-001, AV-005, SPEC.md §File watching

**Context.** F-011 requires every external edit to reach the index, and AV-001 names missed watcher events as the main cause of index drift. ARCHITECTURE.md specified a `VaultWatcher` wrapping `QFileSystemWatcher` on directories, to stay inside the inotify watch limit on large vaults. A directory watch in `QFileSystemWatcher` reports that a folder changed, without saying which file, and does not report a file written in place: only creations, deletions and renames. Editors that write in place, and tools such as `echo >>`, would leave the index stale.

**Options.**
- **A. `QFileSystemWatcher` on directories, re-listing a folder on each change.** Portable; misses in-place writes entirely.
- **B. `QFileSystemWatcher` on directories and every note file.** Sees everything, but one watch per file exhausts `fs.inotify.max_user_watches` on large vaults, which CLAUDE.md §Pitfalls already warns about.
- **C. Linux inotify directly, one watch per folder.** Chosen. A folder watch reports each contained file's close-after-write, creation, deletion, move and attribute change by name, so one watch per folder sees in-place writes too. Linux-only, which D-003 already accepts.

**Decision.** Option C. `VaultWatcher` calls `inotify_init1`/`inotify_add_watch` on every visible, non-ignored folder of a root, reads events through a `QSocketNotifier` on the indexer thread, and debounces them (SPEC.md §File watching).

**Consequences.**
- In-place writes, rename-based saves and deletions are all reported per file; folder-level changes, `.gitignore` edits and queue overflow fall back to a full scan of the root.
- Watch usage is one per folder, not per file; hitting the limit is reported in the status bar with a pointer to BUILD.md.
- A port to another platform would need a new watcher back end; nothing else depends on inotify.

**Reversal conditions.** Revisit if Coeus ever targets a non-Linux platform (D-003), in which case `QFileSystemWatcher` with per-file watches, or the platform's own API, becomes the back end for that platform.

### D-020 Push by running git; read and commit through libgit2
**Decided:** 2026-10-14
**Recorded:** 2026-10-14
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-14)
**Related:** F-022, AV-005, SPEC.md §Git

**Context.** F-022 commits from Coeus and optionally pushes. Everything else Coeus does with Git (history, versions, diff, blame, status, commit) is local and runs through libgit2. Pushing is not local: it needs the author's credentials. Every repository under `~/Projects` pushes to GitHub over HTTPS through Git's `store` credential helper, which libgit2 does not consult; SSH remotes would need the agent wired in by hand. Git hooks, which the author uses (Coeus's own pre-commit hook runs `coeus --lint`), also run only under the `git` command.

**Options.**
- **A. libgit2 throughout.** No dependency on the `git` binary. Coeus would need its own credential callback: the SSH agent for SSH remotes, and for HTTPS reading `~/.git-credentials` or a token from its own config, so Coeus would handle plaintext tokens; push-side hooks would not run.
- **B. Commit through libgit2; push by running `git push`.** Chosen. The push uses the author's credential helper, SSH agent, configuration and hooks exactly as a terminal does, and Coeus never sees a credential.
- **C. Commit only; no push.** F-022 makes push optional, but the author pushes after nearly every commit.

**Decision.** Option B. `git/GitCommand` (first `GitPush`) runs `git -C <workdir> push` as a `QProcess` beside the UI thread, with `GIT_TERMINAL_PROMPT=0` and, unless the author set one, `GIT_SSH_COMMAND="ssh -o BatchMode=yes"`, so a missing credential fails at once instead of waiting for input that never comes, stopped after 120 s. Its result goes to the status bar. Commits are made by libgit2 (SPEC.md §Git).

**Consequences.**
- Push needs `git` on the `PATH`; without it the status bar says so. Everything else works without it.
- Commits made in Coeus do not run Git's commit hooks (`pre-commit`, `commit-msg`), because libgit2 does not run hooks; IMP-005 records that.
- Push output is parsed only for its first `fatal:`/`error:` line or its last line, in the C locale.

**Reversal conditions.** Revisit if libgit2 gains support for Git's credential helpers, or if Coeus must run where no `git` binary is available (then option A, with credentials kept out of plaintext).

**Amended 2026-10-14.** The author extended the decision to fetching: the project board's Fetch (F-032) runs `git fetch --quiet` in each project's repository, one after another, the same way and with the same environment, only when asked and never on a timer. Fetch updates only remote-tracking refs, never the working tree.

### D-021 Claude Code is the MCP client; local models are optional
**Decided:** 2026-10-14
**Recorded:** 2026-10-14
**Status:** Accepted
**Authors:** Shane Hartley (with Claude discussion 2026-10-14)
**Related:** F-033, D-012, D-003, D-008, AV-012, SPEC.md §MCP surface

**Context.** `coeus --mcp` (F-033) serves tools; the model that calls them lives in the client. The author has LM Studio 0.4.26, itself an MCP client, with Qwen3.5 9B Abliterated and Qwen2.5 7B Instruct (both marked for tool use), Qwen2.5 Coder 14B and Nomic Embed Text v1.5. D-012 named Claude chat and Claude Code as the clients. The author needs a client that works on any machine they use, whatever its hardware.

**Options.**
- **A. A local model through LM Studio.** Private, offline and free, but it needs a machine with the memory and GPU to run it and LM Studio installed; 7–9B models chain tool calls and write register entries less reliably, and their default context is too short for a large note.
- **B. A Claude session.** Chosen. Claude Code runs on any machine without local model hardware, chains tool calls reliably, holds whole register files in context, and writes entries to the standard. Notes a tool returns are sent to Anthropic.
- **C. Claude in the browser (claude.ai).** It cannot start a local program, so it cannot use a stdio server; Coeus would need to serve MCP over HTTP.

**Decision.** Option B. Claude Code is the client Coeus's MCP server is built and documented for, registered once per machine with `claude mcp add --scope user` (BUILD.md §MCP server); it serves Claude Code in a terminal and in IDEs such as Antigravity. A local model through LM Studio stays a supported, optional client for private or offline questions, with its own snippet; nothing in Coeus depends on one.

**Consequences.**
- The tools and their error messages are written for a capable model; no extra guidance is added for small ones.
- Registration is per machine and points at that machine's `coeus` binary and vault; the binary is Linux-only (D-003).
- Nomic Embed Text v1.5, served by LM Studio, is a candidate for the embedding model D-008 leaves open; this decision does not choose it.

**Reversal conditions.** Revisit if the author's notes must not leave the machine (then a local model becomes the client and the tools may need simpler results), or if Claude's web and desktop apps gain a way to reach a local stdio server.

### D-022 Mermaid, maths and PDF pages in reading mode
**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (decision), Claude (measurements and recommendation, 2026-10-15)
**Related:** F-023, F-025, D-002, D-009, D-021, SPEC.md §Reading mode

**Context.** Reading mode is a `QTextBrowser`: Qt's rich-text HTML, no JavaScript, images through Qt's image plugins. F-025 asks for Mermaid diagrams and maths to render, and F-023 for PDF embeds to show inline; today they are shown as code, and a PDF as a card that opens it. Live preview, later in Phase 4, will reuse whatever reading mode does. Measured on the author's machine (Linux Mint 22.3, Qt 6.4.2), 2026-10-15:
- Qt WebEngine 6.4.2 is in the archive: `qt6-webengine-dev` brings 21 new packages, 195 MB installed (the Chromium core alone 151 MB).
- Qt PDF 6.4.2 is in the archive separately: `qt6-pdf-dev`, 4 packages, 5 MB.
- Maths through LaTeX: `latex` and `dvisvgm` (both installed here; TeX Live 2023) turn a display formula into a 19 KB SVG in about 0.14 s cold, 0.13 s warm, and Qt's SVG plugin draws that SVG correctly, glyphs as paths.
- Mermaid through a browser: headless Google Chrome (installed here) with Mermaid 11.4.1's 2.6 MB script renders a five-node flowchart in 0.84 s cold, 0.48 s warm, peaking at about 215 MB while it runs. Qt cannot draw Mermaid's SVG: it depends on CSS and `foreignObject`, and Qt's SVG plugin dropped every label and arrow. A diagram must come back as a picture.
- No native C or C++ Mermaid renderer exists. MicroTeX, a C++ LaTeX renderer with a Qt backend (MIT), was last changed 2024-08-06 and has no releases.

**Options.**
- **A. Reading mode in Qt WebEngine.** The reading view becomes a web view with Mermaid, KaTeX and Chromium's PDF viewer: everything renders, exactly as on the web. 195 MB of dependencies, a Chromium process of a few hundred MB per open view, and reading mode, its links, themes and embeds rewritten for a browser; a much larger surface to keep secure and a heavier app to install on a second machine.
- **B. Optional external renderers, cached, plus Qt PDF.** Recommended. Maths renders through `latex` and `dvisvgm` and Mermaid through a Chromium-family browser (Chrome or Chromium) in headless mode with a vendored copy of Mermaid's script (MIT), each only if the tool is on the machine; otherwise the block stays as highlighted code, as now. Renders run off the UI thread, show the code until ready, and are kept in `.coeus/render/` keyed by a hash of the source, the theme and the renderer's version, so each formula or diagram renders once per machine. LaTeX runs with `-no-shell-escape` in a temporary folder; the browser loads only the generated page and the vendored script. PDF pages render natively through Qt PDF (`![[file.pdf#page=3]]`, the first page by default), a 5 MB dependency that would be required, not optional.
- **C. Native libraries only.** Qt PDF for PDFs and MicroTeX vendored for maths, no external programs. Maths would work on any machine, but MicroTeX's maintenance has stalled; Mermaid would stay as code, since nothing renders it natively.
- **D. Leave them as code.** No cost; F-025's Mermaid and maths and F-023's inline PDF dropped from the acceptance. PDFs stay cards.

**Decision.** Option B, chosen by the author 2026-10-15 from the measurements above: maths through `latex` and `dvisvgm` and Mermaid through a headless Chromium-family browser, each when the machine has it, rendered off the UI thread and cached in `.coeus/render/`; PDF pages through Qt PDF.

**Consequences.**
- Mermaid and maths render only on machines with the tools; on others they stay readable code, and a note never changes because of it. The status bar says once which tool is missing.
- `qt6-pdf-dev` becomes a build dependency (BUILD.md), dynamically linked as Qt is (D-009); Mermaid's script is vendored under `resources/` with its licence.
- The render cache is derived data in `.coeus/`, never synced (D-011), and safe to delete.
- Live preview reuses the same rendered images.

**Reversal conditions.** Revisit for Option A if live preview needs layout that `QTextBrowser` cannot do, or if renders through external tools prove too slow or fragile in daily use; for MicroTeX if it is maintained again and maths without TeX matters.

### D-023 Panes and windows restructure the shell
**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (decision), Claude (options, 2026-10-15)
**Related:** F-012, D-002, D-012, SPEC.md §Panes

**Context.** F-012 asks for horizontal and vertical splits, and for a note to be torn off into a window of its own with its own docks, two windows on two monitors keeping independent layouts. The window had one `QTabWidget` as its central widget, used in 56 places and 31 test checks, and owned the vault's whole session: the index service, the embedding worker, the MCP link, the catalogue and the render service. CLAUDE.md said later features are panels, docks or commands, never a restructured shell; F-012 cannot be any of those.

**Options.**
- **A. A workspace of panes, then a shared session.** Chosen. Step 1: a `Workspace` of tab areas in splitters replaces the single tab widget, answering the calls the window made of it with one index across every pane, so the rest of the window and its tests are unchanged. Step 2: the vault's session moves out of the window into an object several windows share, each window with its own panes, docks and saved layout.
- **B. Splits only.** Step 1 alone; no tear-off. Cheaper, but F-012's multi-window half is dropped.
- **C. Notes as dock widgets.** Qt's docks already tab, split and float. But floating docks belong to one window's layout, are not windows with docks of their own, and every panel would share them; the acceptance test cannot be met.
- **D. A process per window.** Two processes would index and embed the same vault into the same database at once, and only one may serve MCP; rejected.

**Decision.** Option A, in two steps, committed separately. A note is open in one place at a time: opening a note already open in another pane or window shows it there, so no two editors ever hold separate buffers of one note. Split Right and Split Down move the current tab into the new pane when its pane keeps another tab; otherwise the new pane starts empty and takes the next note opened.

**Consequences.**
- CLAUDE.md's rule against restructuring the shell is amended: panes and windows are the shell's own business, decided here; panels, docks and commands remain how features join it.
- The window's code keeps calling `m_tabs` as before; only splitting, moving and focusing panes are new.
- The open panes are not restored on restart, as tabs are not.
- Step 2 moves the index service, embedding worker, MCP link, catalogue and render service out of `MainWindow`.
- *Amended 2026-10-16, as built:* step 2 moved what there can be only one of per vault into `ui/VaultSession`, shared by the vault's windows and ended by the last: the index service and embedding worker, the MCP link, the linked roots, bookmarks and recent notes. The catalogue and render service stayed per window after all: the catalogue is a read-only snapshot of the shared index, cheap to load twice, and the render service's work is cached on disk, so a second window redraws nothing already drawn. A note moving window takes its unsaved text, cursor and mode but not its undo history, since a `QTextDocument` cannot change hands between editors without carrying the first editor's layout.

**Reversal conditions.** Revisit if the author wants the same note side by side in two panes, which needs editors sharing one buffer and live preview's folding made per view; or if step 2's session split proves too disruptive, in which case Option B stands and the tear-off is withdrawn from F-012.

### D-024 Sync with rclone bisync: one pass, this machine's copy wins, the other kept beside it
**Decided:** 2026-10-17
**Recorded:** 2026-10-17
**Status:** Accepted
**Authors:** Shane Hartley (order, remote, writes), Claude (options and measurements, 2026-10-17)
**Related:** F-028, F-029, D-007, D-011, AV-004, AV-005, SPEC.md §Sync

**Context.** Phase 5 begins with F-028: the vault synced to Google Drive through an rclone remote, with F-029 (native Drive) decided later, after living with rclone, as D-007's reversal condition allows. SPEC.md sketched a provider with separate `pull()` and `push()` and `rclone bisync --conflict-resolve none`, and CLAUDE.md's invariants said Coeus never writes a vault file on sync. Measured with rclone v1.75.2 on two vault folders and a third standing in for Drive: `--conflict-resolve none` renames *both* versions of a note edited on two machines (`note.md.conflict1`, `note.md.conflict2`), so the note itself vanishes from both until someone notices, and an editor showing it reports it deleted. The first run against a remote needs `--resync`, which copies either way and, where both sides hold a file that differs, overwrites one with the other: a laptop holding an old copy of the vault would replace the desktop's newer notes without a word. A run where every file changed stops with a safety abort.

**Options.**
- **A. Separate pull and push, as SPEC.md sketched.** bisync is one operation in both directions; splitting it means two one-way `rclone sync`s and a hand-made record of what was deleted where, which is what bisync already keeps.
- **B. One bisync pass, `--conflict-resolve none`.** No loss, but the note disappears behind two renamed copies.
- **C. One bisync pass, `--conflict-resolve path1 --conflict-loser num`; Coeus guards the first resync.** Chosen. The machine that meets the conflict keeps its version as the note and the other arrives beside it as `<name>.conflictN`, on every machine. Before a resync, Coeus lists the files that differ (`rclone check`) and saves the remote's version of each beside the local one as a conflict copy first, so the resync overwrites nothing that is not also kept.

**Decision.** Option C. The author chose rclone now and native Drive later, the official rclone release (1.66 or newer for these flags), a Google Drive remote, and that sync may write vault files.

**Consequences.**
- `SyncProvider` has one `sync()` pass in place of `pull()` and `push()`; conflicts are found and resolved by Coeus, not the provider (`sync/SyncConflicts`), so a later provider can produce the same conflict copies and reuse the resolution.
- CLAUDE.md's write invariant is amended: once the author has configured a remote, sync writes the vault through rclone. A note open with unsaved changes is never overwritten silently: the editor's own conflict prompt (AV-002) shows the disk changed under it. Conflicts are resolved only by the author's command.
- `<name>.conflictN` copies are not notes (not `.md`), so a conflicted register cannot put duplicate IDs in the index; they are shown until resolved.
- bisync's state lives in `.coeus/sync/` (machine-local, never synced), with `--resilient --recover` so an interrupted pass, as when Coeus quits mid-sync, recovers without a resync.
- A safety abort is reported, and only the author's Sync Anyway passes `--force`.
- Encryption (SPEC.md §Sync) stays with F-029; with rclone, a `crypt` remote over Drive does it.

**Reversal conditions.** Revisit if bisync's conflict handling or its safety aborts prove troublesome in daily use, or if the native Drive provider (F-029) is built, which would then decide its own conflict handling but keep the conflict copies' form.

### D-025 Frontmatter values the author declares, in a note in the vault
**Decided:** 2026-10-17
**Recorded:** 2026-10-17
**Status:** Accepted
**Authors:** Shane Hartley (scope and approach), Claude (format, 2026-10-17)
**Related:** F-008, F-015, F-032, D-005, D-011, D-015

**Context.** The Properties card treated every frontmatter `status` in the vault as a project status and every `stage` as an idea stage (F-008 as first specified), so a scene of the author's novel with `status: draft` was flagged as an error, with Active, Dormant, Complete, Archived and Superseded offered instead. Those vocabularies belong to the documentation standard: a project's status is read from its README's header, never frontmatter, and the register lint (F-015) already checks it there; `stage` makes a note an idea only under `ideas/` (F-032). The author wants their own values for their own properties, `draft` among them.

**Options.**
- **A. Add `draft` to the project statuses.** Rejected: the lint would then accept `draft` as a project's status, and the next writer's word would need adding too.
- **B. Suggestions only.** The card offers the values the vault already gives a key, with no configuration; nothing is ever flagged, so a typo (`drfat`) passes unnoticed.
- **C. Declared lists in `.coeus/config.toml`.** Machine-local and never synced (D-011), so the laptop would need its own copy, and Coeus cannot edit a TOML file in its own window.
- **D. Declared lists in a note in the vault, plus suggestions.** Chosen. `Coeus properties.md` at the vault's top: each `## key` heading opens a list of `- value` items, in the order offered; a `Folders:` line limits a list to notes under those folders, the nearest winning; anything else is ignored, so the note can explain itself. It syncs with the vault, is edited in Coeus like any note, and is read again whenever it changes. Keys with no list suggest the values the vault already gives them.

**Decision.** Option D. Coeus's own vocabularies apply only where they mean something: the idea stages for `stage` in notes under the vault's `ideas/`, unless the author declares a list for it there. `status` in frontmatter is the author's.

**Consequences.**
- F-008's acceptance is amended: enums are declared by the author; `status` is no longer validated against the project statuses in frontmatter.
- File › Edit Property Values opens the note, writing a starter that explains the format and declares nothing if it does not exist: an explicit command, through `AtomicWriter`.
- The note is an ordinary note, indexed and searchable; Coeus reads it from disk, so unsaved edits to it apply once saved.
- Suggestions are read from the index's `props` off the UI thread (AV-005), most used first.

**Reversal conditions.** Revisit if authors want vocabularies per template or per note type rather than per folder, or if the note's format proves too loose (a list item mistaken for a value), in which case a stricter format, such as frontmatter in the note, replaces it.
