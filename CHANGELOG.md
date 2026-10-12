# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com).

## [Unreleased]
### Added
- Initial scaffolding: README, CLAUDE.md, FEATURES (F-001–F-031), ROADMAP (Phases 1–6), ARCHITECTURE, DECISIONS (D-001–D-009), SPEC, ATTACK_VECTORS (AV-001–AV-009), BUGS, IMPROVEMENTS (IMP-001), CHANGELOG.
- D-010 linked project roots, D-011 vault-not-index sync with embeddings sidecar, D-012 MCP server.
- Project directory layout: `src/` module folders (including `src/mcp/`), `tests/`, `tools/`, `docs/design/` with the main-window mockup.
- F-032 ideas register and project board, F-033 Coeus MCP server, F-034 linked project roots, F-035 embeddings sidecar.
- AV-010 root path drift, AV-011 sidecar stale vectors, AV-012 register corruption via MCP writes.
- SPEC: §Linked roots, §Ideas, §Embeddings sidecar, §MCP surface; `roots` and `ideas` tables; `root_id` on `notes` and `projects`; root-aware wikilink resolution.
### Changed
- F-017 review-freshness dashboard withdrawn, absorbed into F-032.
- ROADMAP Phase 2 gains F-034; Phase 3 gains F-032, F-033, F-035; Phase 5 acceptance covers two-machine convergence.
