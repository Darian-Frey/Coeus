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

and ends at the next level-2 or level-3 heading or end of file. Headings inside fenced code are ignored. Within an entry, fields are lines matching `^\*\*([A-Za-z][A-Za-z -]*):\*\*( (.*))?$` (single-line fields) or `^\*\*([A-Za-z][A-Za-z -]*)\.\*\*( (.*))?$` (paragraph fields); names may contain hyphens, as the standard's `Trade-offs` does (BUG-006). A paragraph field continues over the following non-blank lines until a blank line, another field or a heading, so `**Options.**` followed by a list keeps the list. Both forms are stored under the same field name; the trailing `:` or `.` is stripped. An entry's status is the first line of its `Status` field, as written.

Every field whose name is `Related` or begins `Related ` (`Related decisions`, `Related claims`) is a citation list: each `(F|C|D|AV|BUG|IMP)-\d+` in it becomes a cross-reference edge, made canonical (three or more digits, so `D-5` is `D-005`), once per target, never to the entry itself, with the line it is on. Separately, every ident with three or more digits anywhere in the entry (title, fields, prose; not fenced code) is a *mention*, once per ident with its first line, never the entry itself. FEATURES, BUGS and IMPROVEMENTS entries cite in prose, so back-references (L4) are judged on mentions; unresolved and stale citations (L1, L2) only on Related fields, so prose may name another project's IDs freely.

**Membership.** A register file is recognised by name anywhere under a project folder (`docs/BUGS.md` counts). A project is a folder whose `README.md` opens with the status header; a note belongs to the nearest such folder above it in its own root, so projects nest. A root's own folder is a project when its top-level README carries the header, as a linked project repository's does (F-034); its project path is the empty string, never NULL (BUG-007). Entries in register files outside every project are indexed with no project and take part in nothing else. `ProjectResolver` recomputes projects, note membership and citation targets after every index change, keeping existing project ids, and writes only rows that differ. A citation resolves to the entry with that ident in the citing entry's project; if the ident is duplicated (AV-003), to the first by note path and line, never by row id, so an incrementally updated index and a rebuilt one agree (AV-001).

### ID allocation

`NextId(project, kind)` = `max(existing IDs of kind in project, unexpired reservations of kind in project, IDs in an open unsaved buffer of the register file) + 1`, zero-padded to at least three digits. IDs are never reused. "Existing" covers every register file of that kind in the project, nested ones included (`docs/DECISIONS.md`), but not those of nested projects. If the register file does not exist, the first ID is `001` and the file is created from the standard's skeleton.

The GUI and the MCP server are separate processes sharing `index.db`. `NextId` runs inside a `BEGIN IMMEDIATE` transaction, so SQLite's write lock serialises allocation across both. Inside it, existing IDs are read by parsing the register file on disk, not from `registers`, because the index can lag the file by the watcher debounce. The chosen number is inserted into `reservations` with `expires = now + 60` and the transaction commits. Expired rows are deleted on each call; a reservation is not removed early when its entry is written, since by then the file holds the ID anyway. The GUI calls `NextId` on a worker thread (AV-005), passing the open buffer of the project's register file of that kind if there is one, so an entry inserted but not yet saved keeps its ID after the reservation lapses.

Reservations are transient, not vault data, so losing them in an index rebuild loses nothing (AV-001). The last guard is at write time. "Insert next ID" changes only the editor's buffer, so the save path's conflict check refuses to overwrite a register file changed on disk since it was loaded (§Editing, AV-002). `append_entry` (F-033) will re-parse the register file immediately before its atomic write and fail if the ID already exists (AV-003).

### Register panel and commands

**Register panel** (F-015). A Registers dock, tabbed with the Explorer, shows one project's entries: a project chooser, a kind filter, a status filter offering the statuses present for the chosen kind as written, and a fuzzy filter over ID and title. Entries are listed by kind then number, with ID, title and status. The project follows the current tab when the note is in a project, and otherwise stays put. Clicking an entry opens its register file at its line. The panel reads the `NoteCatalogue` snapshot and so updates whenever the index reports a change, never querying on the UI thread.

**Insert Next ID…** (`Ctrl+Shift+I`, also in the palette) asks which register, listing the current register file's kind first. With a note in a project open, it allocates the ID, creates the register file from the skeleton if missing (an explicit user command, through `Vault::writeNote` and `AtomicWriter`), opens it, appends the kind's entry skeleton after a blank line, and selects the `Title` placeholder so typing replaces it. The register file is then saved like any edit. Outside a project the command explains why and does nothing.

**ID completion.** In source mode, when the text before the cursor ends with `(F|C|D|AV|BUG|IMP)-\d*` not joined to a preceding word character or `-`, the completion popup (§Wikilink completion) lists the current project's entries of that kind whose number starts with the typed digits, with title and status. Choosing one inserts the ID alone, without `]]`. A note outside every project gets no ID completion.

### Lint rules

`registers/Lint` checks every entry inside a project (F-016); entries outside every project are never linted. `LintResolver` runs it after `ProjectResolver` on every index change, in the same transaction, and writes only `lint` rows that differ; a pass over 704 real entries takes about 12 ms in a debug build. L8 depends on the date, so it is not stored: `IndexQueries::problems` evaluates it when problems are read.

| Rule | Level | Line | Condition |
|------|-------|------|-----------|
| L1 unresolved-ref | error | the citation | A Related field cites an ID that does not exist in the project |
| L2 stale-ref | warning | the citation | A Related field cites an entry whose status term is Superseded, Withdrawn, Refuted, Deprecated, wontfix or declined; not when the target is "Superseded by" the citing entry itself |
| L3 bad-vocab | error | the field | A Status, Priority, Domain, Severity or Effort field (as §Vocabularies gives them per kind), or a README's project status, does not open with a vocabulary term |
| L4 one-way-ref | warning | the mention | A mentions B, the standard requires B to mention A back (D↔AV, F, C; AV→D, C; F→D; C→D, AV; BUG→AV, D, IMP, BUG; IMP→BUG, D, IMP: Maintenance Rule 6 and rule 4 of the BUGS and IMPROVEMENTS sections), and B does not |
| L5 missing-reversal | error | the heading | A D- or C- entry has no `Reversal conditions` field |
| L6 missing-tradeoffs | error | the heading | An IMP- entry has no `Trade-offs` field |
| L7 missing-detection | error | the heading | An AV- entry has no `Detection` field |
| L8 stale-review | info | `Last reviewed` | Project `Last reviewed` is older than `review_threshold_days` (default 14), or not a date |
| L9 duplicate-id | error | the heading | An ident already used by an earlier entry (by path, then line) in the project (AV-003) |

**Vocabulary terms.** Values are case-sensitive. A field passes L3 when its first line is a term, or a term followed by a character that cannot continue a word and then anything: the standard's registers qualify their statuses as `fixed (2026-10-05)`, `Complete (Phase 2, 2026-10-13): …` or `applied 2026-09-23`. `Superseded by D-NNN` is matched as `Superseded by D-` and digits. `Completed`, `Medium` and `Done` fail.

L4 is the rule `tools/check_xrefs.py` applies, and gives the same findings on Coeus's own registers.

### Problems panel

A Problems dock below the editor (`Ctrl+Shift+M`; hidden until asked for) lists the lint problems of the current note's project, or of every project, filtered by level, grouped by file with counts, from the `NoteCatalogue` snapshot. Clicking a problem opens its file at its line. With a note in a project open, the status bar shows that project's errors and warnings (`✖ 2  ⚠ 1`); clicking it opens the dock.

In the editor each problem in the open note is underlined with a wavy line, red for errors, amber for warnings, blue for info, under its subject on its line: the cited ID, the field's value, the entry's ID. Hovering shows the message. Problems come from the index, so they reflect the note as last saved; one whose line no longer holds its subject is not shown, and they refresh when the index catches up.

## Index schema

SQLite through the C API (D-018), WAL mode, `synchronous=NORMAL`, `PRAGMA foreign_keys=ON`, 5 s busy timeout. Schema version in `meta`. There are no migrations: an index with another schema version, or one that cannot be opened, is deleted and rebuilt from the vault (D-005). Schema version 2 (Phase 3) adds `status_headers`, the register tables, `lint` and `reservations` to version 1's Phase 2 tables; version 3 gives fields and citations their lines, adds `register_mentions`, and gives lint rows a subject (F-016). `ideas`, `chunks` and `vec` arrive later in Phase 3 with further bumps. Opening an index of an older version rebuilds it.

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
  project_id INTEGER REFERENCES projects(id) ON DELETE SET NULL, -- NULL outside any project
  kind TEXT NOT NULL,                 -- F C D AV BUG IMP
  number INTEGER NOT NULL,
  ident TEXT NOT NULL,                -- e.g. 'AV-003'
  title TEXT NOT NULL,
  status TEXT, line INTEGER NOT NULL
  -- No UNIQUE(project_id, kind, number): a duplicate would fail its insert and
  -- vanish; it must surface as a lint error instead (AV-003).
);

CREATE TABLE status_headers (           -- a README.md opening with the status header
  note INTEGER PRIMARY KEY REFERENCES notes(id) ON DELETE CASCADE,
  status TEXT NOT NULL, provenance TEXT, last_reviewed TEXT, why TEXT
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
  name TEXT NOT NULL, value TEXT NOT NULL,
  line INTEGER NOT NULL
);

CREATE TABLE register_refs (            -- citations in Related fields
  src INTEGER NOT NULL REFERENCES registers(id) ON DELETE CASCADE,
  target_ident TEXT NOT NULL,
  target INTEGER REFERENCES registers(id) ON DELETE SET NULL,
  line INTEGER NOT NULL
);

CREATE TABLE register_mentions (        -- every ident anywhere in the entry (L4)
  src INTEGER NOT NULL REFERENCES registers(id) ON DELETE CASCADE,
  ident TEXT NOT NULL,
  line INTEGER NOT NULL
);

CREATE TABLE lint (                     -- L1–L7, L9; L8 is evaluated when read
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  rule TEXT NOT NULL, level TEXT NOT NULL,
  line INTEGER, message TEXT NOT NULL,
  subject TEXT NOT NULL DEFAULT ''    -- text on the line to underline
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

`term` — FTS5 match on title or body, as a prefix (`arch` finds `architecture`), so results appear while typing. `"phrase"` — exact. `-term` or `-"phrase"` — exclude notes matching it. `tag:x` — notes tagged `x` or beneath it (`#` optional, any case). `path:x` — the display path (`projects/…` in the vault, `pontus/…` in a linked root) starts with `x`; `%` and `_` are literal. `file:x` — the title contains `x`, any case. `re:/pattern/` — the note's text matches the Qt regular expression (which may contain spaces), applied in SQL after the other conditions narrow the notes. Everything is ANDed. Only words and phrases can be excluded; `-tag:` and the like are an error, as is an invalid pattern. A word of punctuation alone is ignored, so a half-typed `-` does nothing.

Results, at most 200, are ordered by BM25 with title matches weighted ten times body matches when the query has words or phrases, otherwise by display path. Each carries a snippet with the matches marked. The Search dock is the third tab beside Explorer and Tags (`Ctrl+Shift+F` focuses it); each search runs off the UI thread 150 ms after typing pauses, a newer one supersedes an older, and the shown results re-run whenever the index changes. Opening a hit selects the first occurrence of a phrase, word or `file:` value in the note. F-006's budget is 100 ms on 10,000 notes; `tests/perf/test_search_speed.cpp` measures 0–29 ms across representative queries in a debug build.

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

## Wikilink completion

Typing in source mode opens a popup when the text before the cursor on its line matches `\[\[([^\[\]|\n]*)$` (F-004). Only typed keys open or narrow it; reloads and programmatic edits never do.

- **Candidates** come from `NoteCatalogue`, a snapshot of every indexed note with its aliases, reloaded on a background thread each time the index reports a change. Notes not yet indexed do not appear until they are.
- **Ranking** uses the command palette's fuzzy match over titles and aliases, up to 50 results. Equal scores prefer the source note's root, then the folder sharing the most leading segments with the source's, then the shorter label.
- **Inserted text** always resolves to the chosen note (§Wikilink resolution, AV-006): the bare title when no other note in the index has it; otherwise the root-relative path without `.md` from the same root, or `root:path` from another root. An alias inserts `link|Alias`.
- **Headings**: after `[[Note#` the popup lists that note's headings (`[[#` lists the current note's), and inserts the heading text.
- **Keys**: Up/Down move, Return or Tab inserts, Escape closes. If `]]` already follows the cursor it is reused, otherwise it is added; the cursor ends after it.
- **Register IDs**: the same popup completes `D-`, `AV-0` and the like to the project's entries (§Register panel and commands).

## Backlinks

The sidebar's Backlinks section follows the current tab (F-005). `Backlinks::collect` runs on a background thread and a newer request discards an older result.

- **Links**: every link the index resolved to the note (§Wikilink resolution), wiki or Markdown, from any root, excluding the note's links to itself. Grouped by source note in root order then path; each shows its line, trimmed and cut to about 160 characters, with `↪` for embeds. Unresolved and ambiguous links are not backlinks.
- **Unlinked mentions**: the note's title and aliases of 3 characters or more, searched with FTS5 (`body : ("title" OR "alias")`, at most 100 notes) and confirmed line by line: a case-insensitive whole-word match (not joined to a letter, digit or `_`) outside wikilinks, Markdown links, code spans, fenced code and frontmatter. One entry per line.
- Clicking a note or a line opens the source note with the cursor on that line. The section refreshes when the tab changes and whenever the index reports a change, so a link written elsewhere appears once that note is indexed.

## Outline

The sidebar's Outline section shows the current tab's headings (F-013). It is built from the editor's buffer, not the index, so unsaved headings appear: `NoteParser` re-parses the buffer on a background thread 300 ms after the text stops changing, and a newer parse supersedes an older one. Headings are those of §Markdown dialect (empty ones excluded); each nests under the nearest preceding heading of a lower level, so a skipped level still nests.

Clicking a heading in source mode puts the cursor on its line and scrolls it to the top; in reading mode it scrolls the rendered heading to the top, matched by text and occurrence (for repeated headings), falling back to position. Moving the cursor highlights the section it is in: the last heading at or above its line.

## Tag pane

The Tags dock shares the left area with the Explorer as a tab (`Ctrl+Shift+T` toggles it) (F-007). It lists every tag in the index, inline and frontmatter alike (§Markdown dialect), nested on `/`, with the number of distinct notes carrying the tag or any tag beneath it. Tags compare without regard to case, as in Obsidian; a group shows the spelling met first in root and path order. The list reloads off the UI thread whenever the index changes, and a filter box narrows it with the palette's fuzzy match, keeping parents of matches.

Activating a tag, in the pane or as a `coeus-tag:` link in reading mode, filters the Explorer: its folder trees give way to a flat list of the notes tagged with it or beneath it (`#a` includes `#a/b`, not `#ab`), sorted by display path, under a "#tag · N notes" banner with Clear. The list follows index changes while shown. Clicking a note opens it; Clear, or opening another vault, restores the trees.

## Properties panel

In source mode a collapsible "Properties (N)" card sits above the editor (F-008); reading mode shows the properties table of §Reading mode instead. It lists the frontmatter's top-level keys in order, re-read from the buffer 300 ms after it changes.

- **Types** are those of §Frontmatter property types, plus **enum** for keys with a fixed vocabulary: `status` takes the project statuses and `stage` the idea stages (§Vocabularies, §Ideas; `registers/Vocabulary`). Enums are drop-downs; everything else is a line edit, lists as comma-separated items.
- **Validation.** An enum value outside its vocabulary (or a list where one value belongs) is shown as written, with an error listing the allowed values, and counted in the card's header; it is never rewritten until the author chooses a valid value. A number field accepts only a number and a date field only a valid `YYYY-MM-DD`; anything else is refused with nothing changed.
- **Editing** changes the editor's buffer, not the file, as one undo step; the note is then saved like any other edit. Only the key's own lines change (D-015): its line and any continuation lines (indented lines or block-list items) become one `key: value` line, keeping the key exactly as written; every other line, comment and the key order stay as they were. A new key is added just before the closing delimiter, with frontmatter created at the top if there was none; an enum key starts at its first allowed value, any other key at `""`. Removing a key removes its lines. Text is written plain when YAML reads it back as the same text, otherwise double-quoted (so `2026` typed into a text field stays text); lists are written in flow style, quoting items that hold `,`, `[`, `]`, `{` or `}`.
- **Invalid YAML** makes the card read-only and shows the parser's message.

## Daily notes and templates

**Daily notes** (F-009). `Ctrl+D`, or File › Open Today's Note, opens today's note at `<vault>/<folder>/<date>.md`, where the date is formatted with the Qt date format `format` (ISO 8601 by default; `/` in it makes subfolders, e.g. `yyyy/MM/yyyy-MM-dd`). If the note does not exist it is created first, an explicit user command through `Vault::writeNote` and `AtomicWriter`, its folders made as needed; an existing note is never rewritten. A new note gets the rendered template if one is configured, otherwise `# <file name>`. Settings that would put the note outside the vault, give it no name, or name a template that cannot be read are reported and nothing is created.

**Templates** (F-010). Every `.md` file under the templates folder (inside the vault) is a template. File › Insert Template… lists them in a palette with the fuzzy filter; the chosen one is rendered and inserted into the current buffer as one undo step, then saved like any edit. Into an empty note it goes whole. Otherwise its body goes in at the cursor and its frontmatter keys are added to the note's frontmatter where the note lacks them, with existing keys untouched (§Properties panel editing rules); the cursor ends after the inserted body.

**Variables**, in templates for both: `{{date}}` (`yyyy-MM-dd`), `{{time}}` (`HH:mm`), `{{title}}` (the note's file name without extension), and `{{date:FORMAT}}` / `{{time:FORMAT}}` with any Qt date or time format. `{{next:KIND}}` (KIND one of `F C D AV BUG IMP`), in a template inserted into a note inside a project, becomes that project's next ID of the kind, allocated by `NextId` on a worker thread before the template is inserted (§ID allocation); each kind is allocated once per insertion, so repeated `{{next:D}}` give the same ID. Outside a project, and in daily notes, it is left as written. Anything else in braces is left as written, and anything left is named in the status bar.

## Reading mode

Each tab has a Source | Live | Reading switch (`Ctrl+E` toggles source and reading; Live is Phase 4). Reading mode shows the editor's buffer, unsaved changes included, rendered by `parser/HtmlRenderer` from md4c's callbacks (D-014), and follows the buffer 250 ms after it changes. It never changes the buffer.

- **Frontmatter** becomes a key/value properties table; invalid YAML shows a one-line problem instead.
- **Soft line breaks** render as line breaks, as Obsidian does (CommonMark renders them as spaces).
- **Raw HTML** is shown as escaped text, never interpreted.
- **Callouts** (`> [!kind] Title`) become a box with a coloured left border; the title defaults to the kind.
- **Task lists** show ☐ / ☑; a list whose items are all tasks drops its bullets.
- **Block IDs** are hidden. **Highlights** get a background. **Maths** shows as code until F-025; Mermaid shows as its code block.
- **Images** (`![](path)`, relative to the note; `![[file.png]]`, relative to the note then the root) load only from inside the note's root; a missing or outside image shows as `[target]`. Note embeds (`![[Note]]`) render as `↪` links until transclusion (F-023, Phase 4).
- **Tables** follow md4c: a delimiter cell with a single dash (`:-`) is not recognised; `:--` or longer is.

Links carry their kind in the URL scheme. Clicking `coeus-wiki:` opens the note the index resolved the wikilink to (SPEC §Wikilink resolution, read through `IndexQueries`), and reports ambiguous, unresolved or not-yet-indexed links in the status bar rather than guessing. `coeus-file:` opens the Markdown link's path relative to the note. `coeus-tag:` is reserved for the tag pane. Any other scheme opens in the desktop's handler.

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
| `folder` | `daily` | config.toml `[daily]` |
| `format` | `yyyy-MM-dd` | config.toml `[daily]` |
| `template` | none | config.toml `[daily]` |
| `folder` | `templates` | config.toml `[templates]` |
