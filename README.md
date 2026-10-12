> **Status:** Active
> **Provenance:** Shane Hartley (author); Claude (initial scaffolding 2026-09-25)
> **Last reviewed:** 2026-10-12
> **Why this status:** Scaffold extended 2026-10-12 with linked roots, project board, MCP server and sync model. Phase 1 (GUI shell) complete 2026-10-12; Phase 2 (core vault) next.

# Coeus

Coeus is a local-first knowledge base for Linux desktop, built in C++23 and Qt6. A vault is a plain folder of Markdown files — Coeus never owns the data, only an index. It covers the core of what Obsidian does (wikilinks, backlinks, live-preview editing, full-text search, tags, properties, daily notes, templates) and adds three things Obsidian cannot do natively: it understands the project-scaffold documentation standard (F/D/C/AV/BUG/IMP registers as typed objects, not text), it does local semantic search through the Mnemosyne memory subsystem with a related-notes sidebar, and it treats Git as a first-class citizen with per-note history, blame and diff. Sync is a pluggable provider layer with Google Drive as the intended first native target.

Named for the Titan of intellect and the celestial axis — the pole everything turns around.

## Quick Start

```bash
git clone https://github.com/Darian-Frey/coeus.git
cd coeus
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/coeus ~/notes
```

Verified 2026-10-12. See [BUILD.md](BUILD.md) for dependencies, tests and troubleshooting.

## Build requirements

- Linux (x86_64). Developed on Linux Mint; Kubuntu also targeted.
- C++23 compiler (GCC 13+ or Clang 17+); `std::expected` is required (D-013)
- CMake 3.25+
- Qt 6.4+ (Widgets, Gui, Core, Concurrent, Test) — the version packaged by Linux Mint 22 and Ubuntu/Kubuntu 24.04
- SQLite 3.40+ with FTS5, plus the `sqlite-vec` extension
- libgit2 1.7+
- An embedding model runtime for Mnemosyne (see `SPEC.md` §Embeddings — decision pending, D-008)

## Project structure

```
coeus/
├── src/
│   ├── app/          # main(), QApplication setup, settings
│   ├── ui/           # QMainWindow shell, dock panels, editor widgets
│   ├── vault/        # filesystem access, atomic writes, file watcher
│   ├── parser/       # Markdown, wikilink, frontmatter, register parsers
│   ├── index/        # SQLite schema, FTS5, sqlite-vec, incremental indexer
│   ├── registers/    # scaffold-aware register model and validation
│   ├── search/       # full-text and semantic query layer
│   ├── git/          # libgit2 wrapper: history, blame, diff, commit
│   ├── sync/         # SyncProvider interface and implementations
│   └── mcp/          # `coeus --mcp` server: read and write tools
├── tests/            # Qt Test / Catch2 suites
├── tools/            # check_xrefs.py and other repo tooling
└── docs/             # domain documentation beyond the scaffold set
    └── design/       # UI mockups (main-window.png)
```

`app/`, `ui/` and `vault/` hold the Phase 1 shell; the other module folders are empty placeholders (`.gitkeep`). Scaffold documents live at the repository root.

## Documentation

- [Build](BUILD.md)
- [Features](FEATURES.md)
- [Roadmap](ROADMAP.md)
- [Architecture](ARCHITECTURE.md)
- [Decisions](DECISIONS.md)
- [Specification](SPEC.md)
- [Attack vectors](ATTACK_VECTORS.md)
- [Bugs](BUGS.md)
- [Improvements](IMPROVEMENTS.md)
- [Changelog](CHANGELOG.md)
- [CLAUDE.md](CLAUDE.md) — AI session handoff

## Licence

MIT — see [LICENSE](LICENSE) and D-009 in [DECISIONS.md](DECISIONS.md). Qt6 is used under the LGPL v3 and must be linked dynamically.
