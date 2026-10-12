# Specification

Authoritative technical reference for Coeus. FEATURES.md says what; this says how. Constants and formats defined here are not duplicated elsewhere.

## Vault layout

```
<vault>/
├── **/*.md                 # notes; any depth
├── **/*.{png,jpg,svg,pdf,mp3,mp4}   # attachments
├── ideas/                  # idea notes (§Ideas)
├── .coeus/                 # machine-local; never synced (D-011)
│   ├── index.db            # SQLite, derived, safe to delete
│   ├── config.toml         # per-vault, per-machine settings
│   ├── journal/            # autosave journal (§Autosave journal)
│   └── logs/
├── .coeus-shared/          # synced, hash-keyed derived data (D-011)
│   └── embeddings.bin      # §Embeddings sidecar
└── .gitignore              # `.coeus/` offered on first open (below)
```

On opening a vault inside a Git repository whose rules do not already ignore `.coeus/` and `.*.coeus-tmp-*` (checked with the repository's own rules, off the UI thread), Coeus shows a non-modal prompt listing the missing patterns, so a temporary file left by a crash never shows as untracked:

- **Add to .gitignore** appends the missing patterns under a `# Coeus: …` comment to `<vault>/.gitignore`, creating it if needed, through `AtomicWriter`. A vault that is a subfolder of a repository gets its own `.gitignore`, which Git applies to that folder.
- **Not Now** asks again next time.
- **Don't Ask Again** records `gitignore_offer = "declined"` under `[vault]` in `config.toml`.

The command File › Add Coeus Files to .gitignore does the same on request, whatever was declined. Coeus never edits `.gitignore` unasked.

Coeus edits `config.toml` line by line: it replaces the key's line or adds it under the table's header (appending the table if absent), keeps every other line, comment and blank as written, refuses to touch a file that does not parse, and checks that the result parses with the intended value before writing it through `AtomicWriter`.

A **project** is any folder containing a `README.md` whose first four lines are the scaffold status header (§Status header). Register files are recognised only inside a project folder.

## Linked roots

The vault folder is root 0. Additional roots are listed in `.coeus/config.toml`:

```toml
[[roots]]
name = "coeus"
path = "/home/shane/src/coeus"

[[roots]]
name = "pontus"
path = "/home/shane/src/pontus"
```

Each root gets its own `VaultWatcher` and its own `.gitignore` handling. `notes.path` is relative to the root; `notes.root_id` identifies it. The UI displays `<name>/<path>`. A root whose `path` does not exist on this machine keeps its rows and is shown greyed.

- **Names.** Letters, digits, `.`, `_`, `-`; unique; `vault` is reserved for root 0. A rejected entry is reported and skipped; the others still load. Malformed TOML is reported with line and column, and the vault opens with no linked roots.
- **Paths.** Absolute, `~/…`, or relative to the vault folder.
- **Ids.** Root 0 is the vault. A linked root keeps the id recorded for its name in `roots`; a new name takes the next free id. Renaming a root is removing one and adding another.
- **Absent roots** (`path` missing): `roots.present = 0`; the root is neither scanned nor watched, so its rows survive untouched (AV-010).
- **Removed roots** (no longer in config): the root and every row derived from it are deleted.
- **Ignore rules.** Paths a Git repository ignores are neither indexed nor watched, using Git's own rules through libgit2: nested `.gitignore` files, negation, `.git/info/exclude` and global excludes. Two kinds of repository apply: the one containing the root (found by searching upward; disregarded if it ignores the root itself), and repositories nested inside the root, recognised when the walk reaches a visible, non-ignored folder holding `.git`. A path is judged by the innermost repository containing it; outside every repository nothing is ignored. Editing a `.gitignore` rescans the root.
- **Temporary files** from interrupted saves are swept from every present root on open (IMP-002).
- **Explorer.** The vault and each linked root, in config order, is a collapsible section with its own tree; the vault's header is its folder name. An absent root is a greyed "missing on this machine" header with no tree. The root list is read from `config.toml` when the vault opens, on the UI thread, so the tree does not wait for the first index scan. Notes from any root open in tabs labelled `<name>/<path>`; paths outside every root are refused. Roots are machine-specific: `config.toml` lives in `.coeus/`, which never syncs (D-011). Only root 0 passes through the sync provider.

## Ideas

Notes under `<vault>/ideas/` are ideas when their frontmatter carries `stage`:

```yaml
---
stage: idea          # idea | scoped | building | shipped | parked
codename: Theia      # optional until named
repo:                # GitHub URL or linked-root name once one exists
summary: One-line pitch
---
```

The project board lists ideas by `stage`. "Promote" moves the note to `<vault>/projects/<codename>/README.md` (or into a new linked root the author chooses), prepends the four-field status header with `Status: Active`, and writes the standard's skeletons for FEATURES, ROADMAP, DECISIONS, CLAUDE and CHANGELOG. The original idea file is deleted only after the move succeeds.

## Markdown dialect

CommonMark 0.31 plus the following extensions, in order of parse precedence:

| Construct | Grammar | Notes |
|-----------|---------|-------|
| Frontmatter | `---\n<YAML>\n---` (or `...` to close) at byte 0 only, after an optional UTF-8 BOM | YAML 1.2 subset: scalars, lists, flat maps. Unclosed means not frontmatter. Invalid YAML is a lint entry, never a parse failure |
| Wikilink | `[[target]]`, `[[target\|alias]]`, `[[target#heading]]`, `[[target#^block]]`, `[[target^block]]` | `target` is a note title or vault-relative path without `.md` |
| Embed | `![[…]]` with the same target grammar | md4c does not recognise it; `parser/` finds it in plain text (D-014) |
| Markdown link | `[text](path)`, `![alt](path)` (embed) | Local files only: no URL scheme, not `//…`, not `#…`. `path` is percent-decoded and relative to the source note's folder; `#fragment` is a heading |
| Tag | `#` then letters (any script), digits, `_`, `-`, `/`; not preceded by a word character or `#`; at least one non-digit; `/` nests, trailing `/` dropped | Frontmatter `tags:` (list, or comma- or space-separated string; leading `#` stripped) also contributes. Not recognised in code, maths, links or embeds. All-digit tags (`#42`) are excluded, as in Obsidian (D-001) |
| Block ID | `^[A-Za-z0-9-]+` at the end of a paragraph or list item, after whitespace | Not in code |
| Callout | `> [!kind]` optionally `> [!kind]-` (folded) | Kinds: note, tip, info, warning, danger, quote, example, question, abstract |
| Maths | `$…$` inline, `$$…$$` block | |
| Mermaid | fenced ```` ```mermaid ```` | |
| Highlight | `==text==` | |

### Wikilink resolution

1. Exact root-relative path match within the source's root; then `root:path` form (`[[pontus:DECISIONS]]`) for an explicit cross-root link.
2. Exact title match (filename without extension) among notes in the same folder as the source.
3. Exact title match within the same root, nearest folder by common-prefix length wins.
4. Exact title match in any other root, nearest by the same rule.
5. Alias match from any note's frontmatter `aliases:`, same-root first.
6. Otherwise unresolved; the link is indexed with `target_note = NULL` and flagged (AV-006). Multiple candidates at steps 3–5 with equal rank are ambiguous and flagged.

Markdown links (`style = 'markdown'`) resolve by path only: relative to the source note's folder, within the source's root. They never match by title or alias; an unmatched path is unresolved and flagged like a wikilink.

### Frontmatter property types

Each top-level key becomes a `props` row (one per list item). Types are inferred: a YAML sequence is `list`; a plain scalar `YYYY-MM-DD` is `date`; a plain numeric scalar is `number`; anything else, including every quoted scalar, null and nested map, is `text`. `aliases:` (or `alias:`) also feeds wikilink resolution step 5.

## Status header

The first four lines of a project `README.md`:

```
> **Status:** <Active|Dormant|Complete|Archived|Superseded>
> **Provenance:** <free text>
> **Last reviewed:** <YYYY-MM-DD>
> **Why this status:** <free text>
```

Parsed with the regex `^> \*\*(Status|Provenance|Last reviewed|Why this status):\*\* (.+)$` per line; all four must be present in that order or the folder is not a project.

## Vocabularies

Fixed enumerations. Values are case-sensitive and validated by `registers/Lint`.

| Field | Values |
|-------|--------|
| Project status | Active, Dormant, Complete, Archived, Superseded |
| F- status | Not started, In progress, Complete, Withdrawn |
| F- priority | Must, Should, Could, Won't |
| C- status | Proposed, Supported, Refuted, Withdrawn |
| C- domain | Empirical, Theoretical, Mathematical |
| D- status | Proposed, Accepted, Superseded by D-NNN, Deprecated |
| AV- severity | Critical, Major, Minor |
| BUG- status | open, fixed, wontfix, deferred |
| BUG- severity | low, medium, high |
| IMP- status | suggested, applied, declined, deferred |
| IMP- effort | trivial, small, medium, large |
| Roadmap phase status | Not started, In progress, Complete |

## Registers

A register file is one of `FEATURES.md`, `CLAIMS.md`, `DECISIONS.md`, `ATTACK_VECTORS.md`, `BUGS.md`, `IMPROVEMENTS.md` inside a project folder.

### Entry grammar

An entry begins at a level-3 heading matching:

```
^### (F|C|D|AV|BUG|IMP)-(\d{3,})(:)? (.+)$
```

and ends at the next level-2 or level-3 heading or end of file. Within an entry, fields are lines matching `^\*\*([A-Za-z ]+):\*\* (.*)$` (single-line fields) or `^\*\*([A-Za-z ]+)\.\*\* (.*)$` (paragraph fields, continuing until a blank line followed by another field or heading). Both forms are stored under the same field name; the trailing `:` or `.` is stripped.

The `Related` and `Related decisions` / `Related claims` fields are split on `,`; each token matching `(F|C|D|AV|BUG|IMP)-\d+` becomes a cross-reference edge.

### ID allocation

`NextId(project, kind)` = `max(existing IDs of kind in project, unexpired reservations of kind in project) + 1`, zero-padded to at least three digits. IDs are never reused. If the register file does not exist, the first ID is `001` and the file is created from the standard's skeleton.

The GUI and the MCP server are separate processes sharing `index.db`. `NextId` runs inside a `BEGIN IMMEDIATE` transaction, so SQLite's write lock serialises allocation across both. Inside it, existing IDs are read by parsing the register file on disk, not from `registers`, because the index can lag the file by the watcher debounce. The chosen number is inserted into `reservations` with `expires = now + 60` and the transaction commits. Expired rows are deleted on each call. A reservation is removed when its entry is written.

Reservations are transient, not vault data, so losing them in an index rebuild loses nothing (AV-001). The last guard is at write time: `append_entry` and "Insert next ID" re-parse the register file immediately before the atomic write and fail if the ID already exists (AV-003).

### Lint rules

| Rule | Level | Condition |
|------|-------|-----------|
| L1 unresolved-ref | error | A cross-reference token names an ID that does not exist in the project |
| L2 stale-ref | warning | Target entry status is Superseded, Withdrawn, Refuted, Deprecated, wontfix, or declined |
| L3 bad-vocab | error | A field value is outside its vocabulary (§Vocabularies) |
| L4 one-way-ref | warning | A cites B but B's Related fields do not cite A |
| L5 missing-reversal | error | A D- or C- entry has no `Reversal conditions` field |
| L6 missing-tradeoffs | error | An IMP- entry has no `Trade-offs` field |
| L7 missing-detection | error | An AV- entry has no `Detection` field |
| L8 stale-review | info | Project `Last reviewed` older than `review_threshold_days` (default 14) |

## Index schema

SQLite through the C API (D-018), WAL mode, `synchronous=NORMAL`, `PRAGMA foreign_keys=ON`, 5 s busy timeout. Schema version in `meta`. There are no migrations: an index with another schema version, or one that cannot be opened, is deleted and rebuilt from the vault (D-005). Schema version 1 holds the Phase 2 tables plus `projects`; `ideas`, `reservations`, the register tables, `lint`, `chunks` and `vec` arrive with Phase 3 and a version bump.

```sql
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
  -- schema_version, embed_model, embed_dim

CREATE TABLE roots (
  id INTEGER PRIMARY KEY,             -- 0 is the vault folder
  name TEXT UNIQUE NOT NULL,
  path TEXT NOT NULL,                 -- absolute, this machine
  present INTEGER NOT NULL DEFAULT 1  -- 0 when path missing at last scan
);

CREATE TABLE notes (
  id INTEGER PRIMARY KEY,
  root_id INTEGER NOT NULL REFERENCES roots(id),
  path TEXT NOT NULL,                 -- root-relative, forward slashes
  title TEXT NOT NULL,                -- filename sans extension
  mtime INTEGER NOT NULL,             -- unix seconds
  size INTEGER NOT NULL,
  hash BLOB NOT NULL,                 -- BLAKE2b-128 of content
  project_id INTEGER REFERENCES projects(id),
  UNIQUE(root_id, path)
);

CREATE TABLE projects (
  id INTEGER PRIMARY KEY,
  root_id INTEGER NOT NULL REFERENCES roots(id),
  path TEXT NOT NULL,                 -- folder containing README.md, root-relative
  status TEXT NOT NULL,
  provenance TEXT, last_reviewed TEXT, why TEXT,
  UNIQUE(root_id, path)
);

CREATE TABLE ideas (
  note INTEGER PRIMARY KEY REFERENCES notes(id) ON DELETE CASCADE,
  stage TEXT NOT NULL, codename TEXT, repo TEXT, summary TEXT
);

CREATE TABLE links (
  src INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  target_note INTEGER REFERENCES notes(id) ON DELETE SET NULL,
  target_raw TEXT NOT NULL,
  style TEXT NOT NULL DEFAULT 'wiki', -- 'wiki' | 'markdown' (§Wikilink resolution)
  heading TEXT, block TEXT, alias TEXT,
  line INTEGER NOT NULL,
  embed INTEGER NOT NULL DEFAULT 0,
  ambiguous INTEGER NOT NULL DEFAULT 0 -- 1 when resolution found equal candidates (AV-006)
);
CREATE INDEX links_target ON links(target_note);

CREATE TABLE tags (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  tag TEXT NOT NULL,
  line INTEGER
);
CREATE INDEX tags_tag ON tags(tag);

CREATE TABLE props (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  key TEXT NOT NULL, value TEXT, type TEXT NOT NULL,  -- text | number | date | list
  line INTEGER
);

CREATE TABLE headings (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  level INTEGER NOT NULL, text TEXT NOT NULL, line INTEGER NOT NULL
);

CREATE TABLE blocks (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  block_id TEXT NOT NULL, line INTEGER NOT NULL
);

CREATE TABLE registers (
  id INTEGER PRIMARY KEY,
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  kind TEXT NOT NULL,                 -- F C D AV BUG IMP
  number INTEGER NOT NULL,
  ident TEXT NOT NULL,                -- e.g. 'AV-003'
  title TEXT NOT NULL,
  status TEXT, line INTEGER NOT NULL,
  UNIQUE(project_id, kind, number)
);

CREATE TABLE reservations (
  project_id INTEGER NOT NULL REFERENCES projects(id),
  kind TEXT NOT NULL,
  number INTEGER NOT NULL,
  expires INTEGER NOT NULL,           -- unix seconds
  holder TEXT NOT NULL,               -- 'gui' or 'mcp:<pid>'
  PRIMARY KEY (project_id, kind, number)
);

CREATE TABLE register_fields (
  reg INTEGER NOT NULL REFERENCES registers(id) ON DELETE CASCADE,
  name TEXT NOT NULL, value TEXT NOT NULL
);

CREATE TABLE register_refs (
  src INTEGER NOT NULL REFERENCES registers(id) ON DELETE CASCADE,
  target_ident TEXT NOT NULL,
  target INTEGER REFERENCES registers(id) ON DELETE SET NULL
);

CREATE TABLE lint (
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  rule TEXT NOT NULL, level TEXT NOT NULL,
  line INTEGER, message TEXT NOT NULL
);

CREATE VIRTUAL TABLE fts USING fts5(
  title, body, path UNINDEXED, tokenize='unicode61 remove_diacritics 2'
);
  -- rowid = notes.id. Stores its own copy of the text so snippet() and
  -- highlight() work; the copy is derived and rebuilt like everything else.

CREATE TABLE chunks (
  id INTEGER PRIMARY KEY,
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  heading TEXT, start_line INTEGER, end_line INTEGER,
  hash BLOB NOT NULL
);

CREATE VIRTUAL TABLE vec USING vec0(
  chunk_id INTEGER PRIMARY KEY,
  embedding float[<embed_dim>]       -- dimension from meta.embed_dim
);
```

Indexed files: `*.md` under each root, excluding any path with a segment beginning `.` (so `.coeus/`, `.git/` and temporary files), folder symlinks, and anything the root's Git ignore rules exclude (§Linked roots). `title` is the filename without its last extension.

Incremental update: on a file event the indexer compares `hash`; a file with unchanged content only has `mtime` and `size` refreshed. A changed file has its rows deleted by cascade and re-inserted in one transaction, keeping its `notes.id`; its `fts` row (not covered by foreign keys) is deleted and re-inserted by `rowid` in the same transaction. Every scan or update ends by recomputing link resolution for the whole index in memory and writing only changed `links` rows, because adding or removing one note can resolve, unresolve or make ambiguous links in any other.

File links are not resolved to notes and not counted as unresolved: wikilinks and Markdown links to attachments (images, PDFs, audio, video, `.canvas`), and Markdown links to any file whose extension is not `.md`.

## File watching

`VaultWatcher` (D-019) holds one inotify watch per visible, non-ignored folder of a present root, with mask `IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF`, not following symlinks. Events for hidden names (`.coeus/`, `.git/`, `AtomicWriter` temporary files) and non-Markdown files are dropped. Note events are collected by root-relative path and flushed together after `watcher_debounce_ms` of quiet as one `Indexer::update` batch (one transaction, one link resolution). A rename-based save arrives as a move onto the note's name and is handled like any write.

A full scan of the root replaces the batch when a folder is created by a move, deleted or moved away, a `.gitignore` changes, or inotify reports a queue overflow. A newly created folder is watched at once and its existing notes reported. If `inotify_add_watch` fails for lack of watches, the status bar says so once per root, pointing to BUILD.md §Large vaults.

Watches are added before a root's first scan, so nothing written during the scan is missed.

## Full-text query syntax

`term` — FTS5 match on title or body. `"phrase"` — exact. `tag:x` — tag filter. `path:x` — prefix on path. `file:x` — substring on title. `-term` — exclude. `re:/pattern/` — SQLite `REGEXP` on body, applied after FTS narrowing. Terms are ANDed.

## Embeddings

Chunking: split at level-1/2 headings, then into windows of at most `chunk_tokens` (default 384) with `chunk_overlap` (default 48). Each chunk's `hash` is BLAKE2b-128 of its text; unchanged chunks are not re-embedded.

The embedding interface Coeus requires from Mnemosyne:

```
embed(texts: [string]) -> [float[dim]]     // batch, normalised to unit length
model_name() -> string
dim() -> int
```

`meta.embed_model` and `meta.embed_dim` are written when the first embedding lands. If either differs at startup from the live model, the `vec` table is dropped and rebuilt (AV-007). Nearest-neighbour uses cosine distance (`vec_distance_cosine`); related-notes (F-019) takes the top 20 chunks for the current note's chunks, aggregates by note with max similarity, excludes the note itself and any note it links to, and returns the top 5.

Model selection is D-008 (pending).

## Embeddings sidecar

`<vault>/.coeus-shared/embeddings.bin` carries vectors between machines (D-011). Little-endian:

```
magic      "COEUSVEC"            8 bytes
version    u32                   1
model_len  u32; model  utf-8     embed_model
dim        u32                   embed_dim
count      u64
entries    count × { hash: 16 bytes (BLAKE2b-128 of chunk text), vec: dim × f32 }
```

Export: after each embedding batch, append new entries; rewrite the header `count`. Import on startup: if `model`/`dim` match the live model, every entry whose hash exists in `chunks` and has no row in `vec` is inserted; others are skipped. Compaction (command and weekly timer): rewrite the file with only hashes present in `chunks`. Concurrent-edit safety: the file is written atomically (§Atomic write); if sync produces a `.conflict-*` sibling, both are imported (union by hash) and the conflict copy deleted.

## Sync

The provider operates on root 0 only. Excluded paths: `.coeus/`, `.git/`, `*.coeus-tmp-*`. Included: everything else, `.coeus-shared/` in particular.

```cpp
class SyncProvider {
public:
  virtual ~SyncProvider() = default;
  virtual std::string name() const = 0;
  virtual std::expected<SyncReport, Error> pull(const Vault&) = 0;
  virtual std::expected<SyncReport, Error> push(const Vault&) = 0;
  virtual std::vector<Conflict> conflicts() const = 0;
  virtual std::expected<void, Error> resolve(const Conflict&, Resolution) = 0;
};
```

`SyncReport` lists files added, changed, deleted, and conflicted. A `Conflict` carries the local and remote versions and the common ancestor if the provider knows it. `Resolution` is `KeepLocal | KeepRemote | Merged(text)`. Providers never delete a local file that has unsaved editor changes; they raise a conflict instead (AV-004).

`RcloneProvider` runs `rclone bisync <vault> <remote>:<path> --conflict-resolve none --conflict-suffix .conflict-{DateOnly}` on a timer (default 300 s) and reports `.conflict-*` files as conflicts.

Encryption, when enabled, is age-style: a vault key derived from a passphrase with Argon2id; each file encrypted with XChaCha20-Poly1305 before the provider sees it. Filenames are also encrypted. Parameters recorded in `.coeus/config.toml` under `[sync.encryption]`.

## MCP surface

`coeus --mcp [--vault <path>]` speaks MCP over stdio. Tools, with JSON argument shapes:

| Tool | Arguments | Returns | Writes |
|------|-----------|---------|--------|
| `search` | `{query, mode: "text"\|"semantic", limit}` | `[{root, path, title, snippet, score}]` | no |
| `related` | `{root, path, limit}` | `[{root, path, title, score}]` | no |
| `get_note` | `{root, path}` | `{frontmatter, body, links, backlinks}` | no |
| `list_registers` | `{project, kind?, status?}` | `[{ident, title, status, fields}]` | no |
| `project_status` | `{project?}` | `[{name, status, last_reviewed, phase, unreleased, git: {branch, ahead, behind, dirty}}]` | no |
| `list_ideas` | `{stage?}` | `[{path, stage, codename, summary}]` | no |
| `next_id` | `{project, kind}` | `{ident}` | reserves for 60 s (§ID allocation) |
| `append_entry` | `{project, kind, ident?, title, fields}` | `{ident, path, line}` | yes |
| `write_note` | `{root, path, body, frontmatter?, mode: "create"\|"replace"\|"append"}` | `{path}` | yes |

`project` is a root name or a root-relative project folder. `append_entry` without `ident` allocates via `next_id`; with one, it must equal the next free ID or the call fails (AV-003). Entry text is generated from the standard's entry format for `kind`; `fields` keys are validated against §Vocabularies where applicable. Write tools require `[mcp] allow_writes = true` in `config.toml`; when the GUI is running, each write raises a toast with 10 s undo (which reverts the file through `AtomicWriter`). All writes land in the vault exactly as a UI save would and re-enter the index through the watcher.

## Atomic write

```
tmp = dir(path) / ("." + basename(path) + ".coeus-tmp-" + random)
write(tmp); fsync(tmp); rename(tmp, path); fsync(dir(path))
```

The temporary file is created with `O_EXCL`. If the original existed, its mode is copied exactly and its owner and group are copied where the process is permitted; an ownership failure is logged as a warning and the save proceeds. If `path` is a symlink, the write replaces the link's target and the link is kept. Any failure before the rename removes the temporary file and leaves the original untouched. A failed directory fsync after the rename is logged, not reported as a failed save, because the new content is already in place.

A crash between creating the temporary file and the rename leaves it behind; the original is intact. On vault open, `TempSweeper` walks the vault on a worker thread and deletes files that match all of: a regular file (not a symlink), named `^\..+\.coeus-tmp-[0-9a-f]+$`, last modified more than 60 s ago, and not inside `.git/`. The age limit spares a save in progress in another Coeus process. This is the only deletion Coeus makes without a user command (IMP-002).

## Editing

A note opens in an `Editor` tab in source mode, highlighted by `MarkdownHighlighter` using the grammar of §Markdown dialect (display only; the index never depends on it). The editor remembers the `ContentHash` of the disk version its buffer is based on (the *base*).

- **Save** (`Ctrl+S`) re-reads the file first. If its hash differs from the base, someone else changed it: the save becomes a conflict prompt and the file is not touched (AV-002). Otherwise the buffer is written through `Vault::writeNote`, which refuses paths outside the note's root, and `AtomicWriter`; the written content becomes the base.
- **External change** (F-011). `IndexService` reports the absolute paths of every changed note, and every root it rescans. Each open editor for those paths compares the disk with its base: equal (including the echo of its own save) is ignored; a clean buffer reloads, keeping cursor and scroll; a modified buffer gets the conflict prompt. A deleted note keeps its text, becomes modified, and saving recreates it.
- **Conflict prompt.** Keep My Version (the disk version becomes the base, so the next save overwrites it; a save that triggered the prompt goes ahead), Load Disk Version (the buffer is replaced and its journal entry dropped), or Decide Later (the tab shows ⚠ and the next save asks again). One prompt per editor at a time.
- **Closing** a modified tab asks Save / Discard / Cancel; quitting or opening another vault asks Save All / Discard / Cancel. Discard drops the journal entries.
- Tabs show `●` while modified and `⚠` while in conflict.

## Autosave journal

Every `autosave_interval_s` (5 s) each modified buffer whose text changed since its last entry is written to `<vault>/.coeus/journal/<first 32 hex of SHA-256 of the note's absolute path>.md` through `AtomicWriter`, on a single background thread so entries for a note are written in order. Notes in linked roots are journalled in the vault's `.coeus/`. Format:

```
coeus-journal 1
path: <absolute note path>
base: <hex ContentHash of the base, empty for a note that did not exist>

<buffer, verbatim>
```

A save, Load Disk Version or Discard removes the note's entry. On opening a vault, entries whose text equals the note on disk are deleted; every other entry is offered for recovery: Recover opens each as an unsaved buffer on top of its recorded base (so a note changed since conflicts on save rather than being overwritten), Discard deletes them, Later keeps them. Entries older than their note, or for notes deleted since, are offered too, flagged, because they may hold the only copy of unsaved work (AV-002); SPEC previously offered only entries newer than the note.

## Constants

| Name | Default | Where |
|------|---------|-------|
| `review_threshold_days` | 14 | config.toml `[registers]` |
| `chunk_tokens` | 384 | config.toml `[semantic]` |
| `chunk_overlap` | 48 | config.toml `[semantic]` |
| `related_count` | 5 | config.toml `[semantic]` |
| `sync_interval_s` | 300 | config.toml `[sync]` |
| `autosave_interval_s` | 5 | config.toml `[editor]` |
| `watcher_debounce_ms` | 200 | config.toml `[vault]` |
| `allow_writes` | false | config.toml `[mcp]` |
| `write_undo_s` | 10 | config.toml `[mcp]` |
| `sidecar_compact_days` | 7 | config.toml `[semantic]` |
| `gitignore_offer` | unset; `"declined"` after Don't Ask Again | config.toml `[vault]` |
