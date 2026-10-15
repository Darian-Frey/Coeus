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
│   ├── bookmarks.json      # starred notes and searches (§Bookmarks and recent notes)
│   ├── recent.json         # notes opened most recently (same)
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
- **Added by Promote idea** (§Ideas): a `[[roots]]` table is appended to `config.toml` through `AtomicWriter`, every other byte kept, after checking the name and path against the roots already there; the vault then reopens to watch and index it.
- **Ignore rules.** Paths a Git repository ignores are neither indexed nor watched, using Git's own rules through libgit2: nested `.gitignore` files, negation, `.git/info/exclude` and global excludes. Two kinds of repository apply: the one containing the root (found by searching upward; disregarded if it ignores the root itself), and repositories nested inside the root, recognised when the walk reaches a visible, non-ignored folder holding `.git`. A path is judged by the innermost repository containing it; outside every repository nothing is ignored. Editing a `.gitignore` rescans the root.
- **Temporary files** from interrupted saves are swept from every present root on open (IMP-002).
- **Explorer.** The vault and each linked root, in config order, is a collapsible section with its own tree; the vault's header is its folder name. An absent root is a greyed "missing on this machine" header with no tree. The root list is read from `config.toml` when the vault opens, on the UI thread, so the tree does not wait for the first index scan. Notes from any root open in tabs labelled `<name>/<path>`; paths outside every root are refused. Roots are machine-specific: `config.toml` lives in `.coeus/`, which never syncs (D-011). Only root 0 passes through the sync provider.

## Ideas

Notes under `<vault>/ideas/` (root 0 only, subfolders included) are ideas when their frontmatter carries a non-empty `stage`; the index keeps them in `ideas` (schema 4). A stage outside the vocabulary is still an idea, shown amber on the board and flagged by the properties panel:

```yaml
---
stage: idea          # idea | scoped | building | shipped | parked
codename: Theia      # optional until named
repo:                # GitHub URL or linked-root name once one exists
summary: One-line pitch
---
```

The project board (§Project board) lists ideas by `stage`. **Promote Idea…** (an idea row's right-click menu) asks for the codename (a folder name: letters, digits, spaces, `.`, `_`, `-`, not starting with `.`) and the place: `<vault>/projects/<codename>/`, or `<folder>/<codename>/` outside the vault, which becomes a linked root named for the codename (lower case, spaces as `-`). The folder must not exist or must be empty. Coeus then writes, through `AtomicWriter`:

- `README.md`: the four-field status header (`Status: Active`, provenance naming the idea's path and the date, `Last reviewed` today), then the idea's own `# heading` (or `# <codename>`), its `summary` and `Repository: <repo>` if set, then the rest of its body. The frontmatter is dropped, because the header must open the file.
- Skeletons for FEATURES, ROADMAP (a Phase 0 not started), DECISIONS, CLAUDE and CHANGELOG (`[Unreleased]` with one entry), with no register entries, so no ID is allocated.

Only after every file is written is the idea note removed (its tab closed; an idea with unsaved changes is refused first). For a linked root, the root is then added to `config.toml` (§Linked roots) and the vault reopens. The new README opens in a tab. A refusal or failure is reported in the status bar.

## Project board

The Project Board dock (`Ctrl+Shift+J`, View menu, tool strip; below the editor, tabbed with Problems, hidden until asked for) lists every project and every idea, and nothing on it is kept by hand (F-032, absorbing F-017). Projects come first, by README status in vocabulary order (Active, Dormant, Complete, Archived, Superseded) then name; ideas follow by stage (idea, scoped, building, shipped, parked) then codename or title. Clicking a row opens the project's README or the idea's note.

| Column | Project | Idea |
|--------|---------|------|
| Name | Folder within the vault, or `<root>/<folder>` in a linked root | Codename, else the note's title |
| Status | README status term (the full line on hover) | Stage; amber outside the vocabulary |
| Reviewed | Days since `Last reviewed` (`today`, `12 d`), amber past `review_threshold_days`; `no date` (amber) if it is not one | — |
| Build | From ROADMAP.md: `<phase> — <title> · <its Status> (n of m complete)`, the current phase being the first whose Status does not open with `Complete` (`Feature-complete` does not count); `All m phases complete`; `no ROADMAP` | `summary` |
| Unreleased | Top-level bullets under CHANGELOG.md `## [Unreleased]`, with the newest release and its date on hover; `—` without a CHANGELOG | — |
| Git | Branch, `↑ahead ↓behind` against its upstream as of the last fetch (Fetch, below), `●n` changed files under the project's folder; `not in Git` | `repo` |

**Commands.** A project row's right-click menu offers **Open README** and **Refresh Header…**: after confirmation it sets `Last reviewed` to today, changing only that line. If the README is open, the change is made in its buffer as one undo step and saved through the editor's conflict check (AV-002); if the buffer already had unsaved edits, it is left unsaved for the author. An idea row offers **Open** and **Promote Idea…** (§Ideas). **Fetch** runs `git fetch --quiet` in each project's repository in turn (D-020), only when clicked, then reads the board again; failures go to the status bar and the button's tooltip, never to a dialog.

ROADMAP.md and CHANGELOG.md are looked for in the project folder, then `docs/`, then `Docs/`. A phase is a `## Phase <n> — <title>` heading (outside fenced code) and its Status the first `**Status:**` line under it. The documents and Git are read on the Git worker, never the UI thread: when the dock is shown, a second after the index changes while it is shown, and when the window is activated. The header counts the projects and how many are past the review threshold.

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

Reservations are transient, not vault data, so losing them in an index rebuild loses nothing (AV-001). The last guard is at write time. "Insert next ID" changes only the editor's buffer, so the save path's conflict check refuses to overwrite a register file changed on disk since it was loaded (§Editing, AV-002). `append_entry` (F-033) re-parses the register file immediately before its atomic write and fails if the ID already exists (AV-003).

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

**From a terminal.** `coeus --lint [--strict] [--quiet] [--mentions] <folder>` indexes the folder alone (its Git ignore rules honoured, linked roots not followed) into a temporary index, never the vault's own, and prints each problem as `path:line: level: message (rule)`, then a count on stderr. It exits 0 when there are no errors, 1 on errors, on warnings with `--strict`, or when the folder holds no project, and 2 when it cannot index the folder. `--quiet` prints errors only. `--mentions` adds an error for every ID, outside fenced code, mentioned in a project's note (any note, not only registers) that no entry of that project defines, unless the same line already reports it as L1: the check `tools/check_xrefs.py` made, which it replaces (IMP-004). It is for repositories whose prose should name only their own IDs; vaults that mention other projects' IDs leave it off. Coeus's pre-commit hook and the `lint_coeus_docs` ctest entry run it on Coeus itself.

### Problems panel

A Problems dock below the editor (`Ctrl+Shift+M`; hidden until asked for) lists the lint problems of the current note's project, or of every project, filtered by level, grouped by file with counts, from the `NoteCatalogue` snapshot. Clicking a problem opens its file at its line. With a note in a project open, the status bar shows that project's errors and warnings (`✖ 2  ⚠ 1`); clicking it opens the dock.

**Copy Report** (F-037) puts what the panel shows, with its scope and level, on the clipboard as Markdown for an AI session or an issue (`registers/ProblemReport`): a `# Register problems: <project>` title (with the level when narrowed), the date and counts, then a `## <file>` section per file with one line per problem, `` `<file>:<line>` <level> <rule>: <message> ``, and a `## Rules` section saying what each rule present means. Paths are relative to the project's folder for one project, else `<root>/<path>` (`<path>` in the vault). MCP's `problems` returns the same problems as data.

In the editor each problem in the open note is underlined with a wavy line, red for errors, amber for warnings, blue for info, under its subject on its line: the cited ID, the field's value, the entry's ID. Hovering shows the message. Problems come from the index, so they reflect the note as last saved; one whose line no longer holds its subject is not shown, and they refresh when the index catches up.

## Index schema

SQLite through the C API (D-018), WAL mode, `synchronous=NORMAL`, `PRAGMA foreign_keys=ON`, 5 s busy timeout. Schema version in `meta`. There are no migrations: an index with another schema version, or one that cannot be opened, is deleted and rebuilt from the vault (D-005). Schema version 2 (Phase 3) adds `status_headers`, the register tables, `lint` and `reservations` to version 1's Phase 2 tables; version 3 gives fields and citations their lines, adds `register_mentions`, and gives lint rows a subject (F-016); version 4 adds `ideas` (F-032); version 5 adds `chunks` and `vectors` (F-018). Opening an index of an older version rebuilds it.

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

CREATE TABLE chunks (                 -- schema 5: each note's passages (§Embeddings)
  id INTEGER PRIMARY KEY,
  note INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL,
  heading TEXT NOT NULL,
  start_line INTEGER NOT NULL, end_line INTEGER NOT NULL,
  text TEXT NOT NULL,                 -- as embedded: "<title> › <heading>\n<words>"
  hash BLOB NOT NULL                  -- BLAKE2b-128 of text
);

CREATE TABLE vectors (                -- one per distinct passage, not per chunk row (D-008)
  hash BLOB PRIMARY KEY,
  vec BLOB NOT NULL                   -- embed_dim float32, little-endian, unit length
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

**Modes** (F-018). A switch beside the query box chooses **Words** (the default, described here), **Meaning** or **Both** (§Embeddings, search). Meaning results name the note, `<path> › <heading>` of the passage that matched, and the passage's opening words, and open the note at the passage's first line.

Results, at most 200, are ordered by BM25 with title matches weighted ten times body matches when the query has words or phrases, otherwise by display path. Each carries a snippet with the matches marked. The Search dock is the third tab beside Explorer and Tags (`Ctrl+Shift+F` focuses it); each search runs off the UI thread 150 ms after typing pauses, a newer one supersedes an older, and the shown results re-run whenever the index changes. Opening a hit selects the first occurrence of a phrase, word or `file:` value in the note. F-006's budget is 100 ms on 10,000 notes; `tests/perf/test_search_speed.cpp` measures 0–29 ms across representative queries in a debug build.

## Embeddings

**Chunking** happens at index time (`search/Chunker`), so indexing never needs the model. A note's body, without its frontmatter, is split at level-1 and level-2 headings outside fenced code, and in a register file also at every entry heading (`### D-008 …`), so a match names the entry. Each section is cut into windows of at most `chunk_tokens` (default 384) with `chunk_overlap` (default 48) repeated between neighbours. Tokens are estimated from words (three words to four tokens), so chunks are the same on every machine and need no tokenizer; the model's 512-token limit leaves room. A chunk's heading is its level-1 heading, `› level-2` under it, or `› <entry>`; its text, which is what is embedded, is `<note title> › <heading>` on the first line and then the words, with line breaks kept; its `hash` is BLAKE2b-128 of that text. The index records how it chunked (`meta` `chunking:<root id>`, the chunker's rule version and both settings); a scan that finds another records chunks every note again, unchanged ones included, without discarding vectors.

**Vectors** are stored once per distinct passage, keyed by its hash (`vectors`), so an unchanged passage is never embedded twice, identical passages share a vector, and the sidecar (F-035) can carry them by hash. A search scans them all: a few thousand 384-float vectors take well under a millisecond (D-008 chose this over `sqlite-vec`). Vectors of passages no note has any more are pruned after each pass.

**Embedding.** In the window, an embedding worker on its own low-priority thread (`search/EmbeddingService`) is woken by every index change and embeds passages without vectors, sixteen at a time, returning to its event loop between batches. The status bar shows `embedding N of M` while it works and `meaning ✓` when every passage has a vector, or `no embedding model` with the reason. Embedding the author's `~/Projects`, 3,661 passages, took about six minutes the first time; changing the chunking rules afterwards re-embedded only the 1,076 new passages, in under two. `coeus --mcp` embeds whatever is missing itself, for at most 2 s before each search by meaning, and reports how much of the vault has vectors.

**Search by meaning.** The query is embedded with the query prefix and compared with every passage; each note keeps its best passage, and notes are ranked by it (cosine similarity, also given to MCP clients as `score`). **Both** (MCP's `hybrid`) merges the word search's ranking with this one by reciprocal-rank fusion (`Σ 1/(60 + rank)`), so a note found both ways comes first; its snippet is the word search's, which shows the match. On the author's vault a search by meaning answers in about 55 ms.

The embedding interface Coeus requires from Mnemosyne:

```
embed(texts: [string]) -> [float[dim]]     // batch, normalised to unit length
model_name() -> string
dim() -> int
```

`meta.embed_model` and `meta.embed_dim` are written when the first embedding lands. If either differs from the model in use, every vector is dropped before any is compared or added, and the vault is embedded again (AV-007). Nearest-neighbour uses cosine similarity; related notes (F-019) score every other note by its best pair of passages with the current note, each pair's cosine less the *typicality* of the current note's passage: its mean cosine with its 10 nearest passages in other notes (a one-sided form of cross-domain similarity local scaling). Scaffold boilerplate, such as a register's preamble or a README's status block, is near its copy in every project, so it is discounted by about as much as it matches and ranks nothing; on the author's projects, raw maximum similarity put five other projects' DECISIONS.md at 1.00 for Coeus's own, where this ranks its ROADMAP, SPEC and BUGS first. The score is relative (typically 0–0.3), not a cosine. The note itself and every note it links to are left out, and the top 5 are shown. Only stored vectors are used: a note's passages not yet embedded take no part, and the sidebar says so.

**Model** (D-008): BAAI's bge-small-en-v1.5, 384 dimensions, at most 512 tokens per passage (longer ones are cut, keeping the closing separator; chunking stays under it), pooled on its [CLS] token and normalised to unit length, so cosine similarity is a dot product. A search query, but not a passage, is prefixed with `Represent this sentence for searching relevant passages: `, as BAAI specifies. `search/Embedder` runs it on the CPU through llama.cpp, eight passages a pass, on half the machine's threads. The model file is `$XDG_DATA_HOME/coeus/models/bge-small-en-v1.5-f16.gguf` (by default `~/.local/share/…`), or `[semantic] model` in `config.toml` (absolute, `~/…`, or relative to the vault); `tools/fetch_model.py` downloads it and checks its SHA-256. Without it Coeus runs and searches text as before, and the semantic features say how to fetch it.

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

Entries are written in hash order, so the same vectors always make the same file. A file whose length does not match its header is rejected whole (`search/Sidecar`).

**Import.** Before the embedding worker embeds anything, and before `coeus --mcp` embeds for a search, the sidecar and its conflict copies are read if they have changed since this index last read or wrote them (their names, sizes and times are kept in `meta.sidecar_stamp`). If a file's `model` and `dim` are the model in use, every entry whose hash is a passage in `chunks` without a vector is inserted; the rest are skipped. A file of another model or dimension, or one that cannot be read, is not imported, with a log line (`coeus.search`) and a status-bar message (AV-007, AV-011).

**Export.** When an embedding pass ends having embedded anything, once after each opening of the vault, and whenever a conflict copy is present, the file is rewritten whole and atomically (§Atomic write) as the union by hash of itself, its conflict copies and the index's vectors, and only if that adds a vector or folds in a copy. Appending in place, as first specified, would let a sync tool copy a half-written file. The union keeps vectors this machine has no passage for (a note not yet synced here, a linked root it lacks) until compaction. A sidecar of another model, or one that cannot be read, is replaced. `coeus --mcp` exports after it has embedded something. On the author's 3,771 passages the file is about 5.8 MB, so a sync tool re-sends that after each edit is embedded. In a vault that is a Git repository it is one of Coeus's own files (§Git): never counted as a change or offered for a commit (BUG-008).

**Conflict copies** are files beside it whose names start `embeddings`, contain `conflict` and do not start with a dot (`embeddings.bin.conflict1`, `embeddings.conflict-2026-10-14.bin`). Each one that can be read is imported and folded into the next export, then deleted; one that cannot be read is left where it is.

**Compaction** (File › Compact Embeddings Sidecar, and every `sidecar_compact_days` after the first opening, checked when an embedding pass ends) rewrites the file with only the hashes present in `chunks`, after the same union. The first opening starts the clock rather than compacting, so a machine that has just been given the vault does not drop the vectors of roots it lacks.

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

`coeus --mcp [--vault <path>]` speaks MCP over stdio (F-033, D-012): JSON-RPC 2.0, one message per line, protocol versions 2025-06-18, 2025-03-26 and 2024-11-05 (the client's if it is one of them, else the newest). It answers `initialize`, `ping`, `tools/list` and `tools/call`; notifications get no reply; anything else is `-32601`, unparseable input `-32700`, an unknown tool `-32602`. Only protocol messages go to stdout; diagnostics go to stderr. The vault is `--vault`, else the `COEUS_VAULT` environment variable; without either it exits with status 2. It needs no display and no running GUI.

**Index.** It opens the vault's own `.coeus/index.db`, reconciles roots with `config.toml` exactly as the GUI does (`index/IndexRoots`), and before each tool call scans every present root for changes (unchanged files are skipped on mtime and size), at most once every 2 s, so edits made by Claude Code or anything else are seen. With the GUI open both processes write the same index; SQLite serialises them. Against eleven real repositories the first call, which scans everything, took 418 ms and later ones 1–220 ms.

**Results.** A tool's result is its JSON object as `structuredContent` and the same JSON as one text content item. A tool that cannot do what was asked (a missing note, an unknown project, bad arguments) returns `isError: true` with a sentence saying why and, where useful, what would work (the roots, the projects). A project is named as `project_status` names it (`projects/coeus`, `pontus`, `pontus/sub`), case aside, or by its folder's name when only one project has it. Paths are root-relative; `.md` may be left off; a path that escapes its root is refused.

Implemented: the read tools `search` (modes `text`, `semantic` and `hybrid`; by meaning, results carry `heading`, `line` and `score`, or `found_by`, and the answer carries `coverage`), `related`, `get_note`, `list_registers`, `problems`, `project_status` and `list_ideas`, and the write tools `next_id`, `append_entry` and `write_note`. Tools, with JSON argument shapes:

| Tool | Arguments | Returns | Writes |
|------|-----------|---------|--------|
| `search` | `{query, mode: "text"\|"semantic"\|"hybrid", limit}` | `{results: [{root, path, title, snippet, heading?, line?, score?, found_by?}], coverage?}` | no |
| `related` | `{root, path, limit}` | `{root, path, related: [{root, path, title, heading, line, snippet, score}]}` | no |
| `get_note` | `{root, path}` | `{frontmatter, body, links, backlinks}` | no |
| `list_registers` | `{project, kind?, status?}` | `[{ident, title, status, fields}]` | no |
| `problems` | `{project?, level?: "error"\|"warning"\|"info"}` | `{project?, root?, folder?, counts: {error, warning, info}, problems: [{root, path, file, line, level, rule, message, project?}], rules: {L1: meaning, …}}`; `file` is relative to the problem's project folder; the problems of a project exclude those of projects nested in it | no |
| `project_status` | `{project?}` | `[{name, status, last_reviewed, phase, unreleased, git: {branch, ahead, behind, dirty}}]` | no |
| `list_ideas` | `{stage?}` | `[{path, stage, codename, summary}]` | no |
| `next_id` | `{project, kind}` | `{ident}` | reserves for 60 s (§ID allocation) |
| `append_entry` | `{project, kind, ident?, title, fields}` | `{ident, path, line}` | yes |
| `write_note` | `{root, path, body, frontmatter?, mode: "create"\|"replace"\|"append"}` | `{path}` | yes |

**Write tools** (F-033, AV-012) are listed always but refuse, saying how to allow them, unless `config.toml` has `[mcp] allow_writes = true`, read on every call so it takes effect at once. They write only through `NextId` and `AtomicWriter`, as the window does, and the server indexes the result before its next answer.

- **`next_id`** reserves the next free ID of a kind in a project for 60 s (§ID allocation, holder `mcp:<pid>`), counting the register file's unsaved buffer if it is open in the window, and returns it with the register file.
- **`append_entry`** checks everything it can before reserving, so a refusal leaves no gap: the title (one line), field names (letters, spaces, hyphens), vocabulary values (§Vocabularies, a qualifier may follow) and the fields the lint requires (L5–L7), with the reason and the allowed values when it refuses. With `ident` it must be the next free ID or the call fails, naming the one that is (AV-003). It then allocates, renders the entry as `registers/EntryWriter` writes the standard (fields in the kind's order, as Insert Next ID's skeletons; today's date and the kind's starting status where missing; BUG and IMP headings with a colon), re-reads the register file and refuses if the ID has appeared meanwhile, and places the entry: a BUG or IMP into the section for its status (`## Open`, `## Fixed`, `## Suggested`, …), following that section's order (newest first or last), replacing `_None._`; anything else, or with no such section, at the end. A missing register file is created from the standard's skeleton. Returns `{ident, root, path, line, created_file}`.
- **`write_note`** creates (fails if it exists), replaces (keeping the note's frontmatter unless `frontmatter` is given) or appends to a note, at a visible `.md` path inside the root. Register files are refused (use `append_entry`), as are hidden paths, paths outside the root and notes over 1 MB. `frontmatter` values are text, numbers, booleans or lists of text, written as YAML the parser reads back the same.

**With the window open** on the same vault, it listens on a per-user local socket (`$XDG_RUNTIME_DIR/coeus-<hash of the vault path>.sock`, this user only; `ui/McpLink`, `mcp/GuiLink`). Before writing a note or register, the server asks whether it is open with unsaved changes and refuses if so; `next_id` takes the unsaved text into account instead. After each write it reports the file and its previous content, and the window shows a toast over the editor area naming the change, with **Undo** for 10 s: Undo puts the previous bytes back through `AtomicWriter`, or removes a file the write created, but only if the file still holds exactly what was written; anything changed since is left alone and the toast says so. Without a window, writes proceed and nothing is offered back; the vault's Git history is the record. A second window on the same vault does not take over the socket.

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
- **Blocks** (F-024): after `[[Note#^` it lists that note's block IDs, each with the start of its text, matching the typed part against the ID (fuzzily) or the text, and inserts `^id`. They are read from the note's file, or for the current note from its buffer, unsaved IDs included.
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

**New notes** (F-036). File › New Note… (`Ctrl+N`) opens a dialog with the note's name, its root (when there are linked roots), its folder within the root, and a template: Blank or any template. The folder starts as the one selected in the explorer (the selected item itself if a folder, else its folder), else the current note's folder, else the top level. A template whose frontmatter has `coeus-folder: <folder>` (relative to the vault) moves the note there when chosen; the key is removed from whatever a template renders, here, in Insert Template and in daily notes, along with frontmatter it leaves empty. `.md` is added to the name. **Create** stays disabled, with the reason shown, while the name is empty, contains a slash or starts with a dot, the folder is outside the root or hidden, or the note exists; folders not made yet are made. The note is the rendered template (Blank: `# <name>`), with `{{next:KIND}}` allocated as for Insert Template when the note is inside a project, written through `Vault::writeNote` and `AtomicWriter` (an explicit user command), checked again for existence just before, and opened in source mode.

**Variables**, in templates for all three: `{{date}}` (`yyyy-MM-dd`), `{{time}}` (`HH:mm`), `{{title}}` (the note's file name without extension), and `{{date:FORMAT}}` / `{{time:FORMAT}}` with any Qt date or time format. `{{next:KIND}}` (KIND one of `F C D AV BUG IMP`), in a template inserted into a note inside a project, becomes that project's next ID of the kind, allocated by `NextId` on a worker thread before the template is inserted (§ID allocation); each kind is allocated once per insertion, so repeated `{{next:D}}` give the same ID. Outside a project, and in daily notes, it is left as written. Anything else in braces is left as written, and anything left is named in the status bar.

## Git

Coeus reads and commits through libgit2 (`git/GitRepository`); the only uses of the `git` command are `git push` and `git fetch` (D-020). Every libgit2 call runs on the Git worker, a one-thread pool owned by `MainWindow`, never the UI thread (AV-005). Nothing Coeus does with Git writes to the working tree: reading writes nothing, and a commit writes only the index and objects under `.git/`.

**Repository.** A note's repository is the non-bare one whose working tree contains it, found by searching upward from its folder: the vault, a folder inside it, a folder above it, or a linked root (F-034). Notes in different repositories show their own histories.

**History** (F-020). The sidebar's History section shows the current note's branch (`main`; `detached at a41c9e2`; before the first commit, the branch HEAD names) and the commits that changed it, newest first, at most 500: short ID, summary, author date and author, with the full message on hover. A commit is listed when its version of the file differs from every parent's, so a merge that took one side unchanged is not, as `git log` does without `--full-history`. When a commit added the file by renaming another (found by similarity), older commits are followed under the old path, as `git log --follow` does. An untracked note shows "No commits of this note yet"; a note outside every repository, "Not in a Git repository". The section follows the current tab, a version or diff tab included, re-reading only when the note changes; it refreshes when the note changes on disk (a checkout or pull), when the window is activated (commits made in a terminal or by Claude Code), and from its ↻ button. A newer request supersedes an older one still running. Ctrl- and Shift-click select commits without opening them.

**Versions.** Clicking a commit opens the note as it was there in a read-only tab, `<name> @ <short ID>`, under a header naming the path at that commit, the commit, its date and author. It is not an editor: nothing can save it, and the journal and conflict checks never see it. Opening the same version again focuses its tab; opening the note itself goes to the note's own tab.

**Diff** (F-021). Git › Diff with HEAD (`Ctrl+Alt+D`), or History's **Diff vs HEAD**, compares HEAD's version of the note with the working copy: the open editor's buffer, unsaved changes included, or the file if the note is not open; a note HEAD lacks compares with nothing. A commit's right-click menu offers **Open This Version**, **Compare with the Version Before** (the next older commit in the list; the first commit compares with nothing) and **Compare with the Working Copy**; with two selected, **Compare These Two Versions**, older against newer. Each opens a read-only tab, `<name>: <old> ↔ <new>`, showing the whole note in reading order with every removed line (red, `−`) before the lines that replaced it (green, `+`) and old and new line numbers, from libgit2's diff of the two texts. A header gives the lines added and removed and the number of changes, with ↑ ↓ to move between them; the tab opens on the first. Comparing two commits again focuses the open tab; a comparison involving the working copy or HEAD is made afresh in its place.

**Blame.** Git › Toggle Blame (`Ctrl+Alt+B`), or History's **Blame** button, opens a gutter left of the editor naming, for each run of lines from one commit, its short ID and date (repeated at the top of the view when the run starts above it), with the summary and author on hover. Blame is of the buffer as it stands against HEAD (libgit2 blames the file at HEAD, then the buffer against that), so new and unsaved lines show *Not committed*, as does a file HEAD lacks; a last line that has lost its final newline counts as changed, as `git blame` says. While the gutter is open, an edit is blamed again 600 ms after typing stops; a slower, older result never replaces a newer one. The gutter belongs to its editor, and the Blame button follows the current tab. Blame of a 175-line note with 69 commits takes about 40 ms on the Git worker.

**Changed files** (F-022). The status bar shows `● N file(s) changed` for the current note's repository (the vault's when no note is open): every path `git status` would list, untracked files included and ignored ones not, except Coeus's own files (`.coeus/`, `.coeus-shared/`, `.*.coeus-tmp-*`), which are never counted or offered for a commit even where no `.gitignore` excludes them. It is hidden in a clean repository and outside any. It is read on the Git worker when a vault opens, 400 ms after the tab changes or files change on disk, when the window is activated, and after a commit or push.

**Commit.** Git › Commit… (`Ctrl+Alt+C`), or clicking the count, opens a non-modal dialog for that repository: its name and branch, every changed path with its status letter (`M`, `A`, `D`, `R`, `T`, `?` untracked, `U` conflicted) and a tick, a message editor, and **Push after committing** (remembered). Tracked changes and new notes (`.md`) start ticked; other untracked files start unticked. Open notes in the repository with unsaved changes are named, since a commit takes files as they are on disk, with **Save All**, which saves them and lists the changes again. **Commit** needs a tick and a non-empty message. It commits exactly the ticked paths as they are on disk (a missing file as its deletion) on top of HEAD, with the message trimmed and line endings made `\n`, as the identity in Git's configuration (`user.name`, `user.email`), and moves the branch; anything else already staged stays staged and out of the commit, as `git commit -- <paths>` does. Paths outside the working tree and Coeus's own files are refused, as are a repository with unresolved conflicts and a selection unchanged since HEAD. A refusal is shown in the dialog, which stays open; success closes it, names the commit in the status bar, and refreshes History, the count and any open blame gutters. Commit hooks do not run (D-020).

**Push.** With **Push after committing** ticked, a successful commit is followed by `git -C <workdir> push` (D-020), run as a background process that may not prompt: it fails at once without a usable credential and is stopped after 120 s. The outcome, git's own words, goes to the status bar and the count's tooltip; a failure never opens a dialog or blocks the window. One push runs at a time.

## Reading mode

Each tab has a Source | Live | Reading switch (`Ctrl+E` toggles source and reading; Live is Phase 4). Reading mode shows the editor's buffer, unsaved changes included, rendered by `parser/HtmlRenderer` from md4c's callbacks (D-014), and follows the buffer 250 ms after it changes. It never changes the buffer.

- **Frontmatter** becomes a key/value properties table; invalid YAML shows a one-line problem instead.
- **Soft line breaks** render as line breaks, as Obsidian does (CommonMark renders them as spaces).
- **Raw HTML** is shown as escaped text, never interpreted.
- **Callouts** (`> [!kind] Title`) become a box with a coloured left border; the title defaults to the kind.
- **Task lists** show ☐ / ☑; a list whose items are all tasks drops its bullets.
- **Block IDs** are hidden. **Highlights** get a background. **Maths** shows as code and Mermaid as its code block until the rendering decision for F-025 (see ROADMAP Phase 4).
- **Fenced code** with a language is coloured by `parser/CodeHighlighter` (F-025): keywords, types and built-ins, strings, comments and numbers, in the theme's code colours (§Themes and shortcuts). It is a tokeniser per language family, not a parser, and never fails: C and C++, Python, JavaScript and TypeScript (and QML), Rust, Go, Java, Kotlin and C#, shell, JSON, YAML, TOML and INI, SQL, CMake, and diffs by line, each under its usual names and file extensions, in any case. Other languages, and code without one, are shown plain.
- **Images** (`![](path)`, relative to the note; `![[file.png]]`, relative to the note then the root) load only from inside the note's root; a missing or outside image shows as `[target]`. Other attachments (PDF, audio, video, canvas) show as a card, `[PDF] spec.pdf`, that opens the file with the desktop's application; PDF pages inline wait on the same rendering decision as Mermaid and maths.
- **Note embeds** (`![[Note]]`, `![[Note#Heading]]`, F-023) render the note's body without its frontmatter, or the heading's section (the heading through the line before the next heading of the same or a higher level, fenced code ignored; the heading matched case aside), in a framed block whose title links to it. The target resolves as a click on the link would, by the index's resolution of that link in the note it is written in (§Wikilink resolution), so an embed inside an embedded note resolves from that note. Embeds nest three deep; deeper, a note embedding itself (or the note being read), an unresolved, ambiguous or unindexed target and a missing heading show as a `↪` link with the reason. `![[Note#^id]]` and `![[Note^id]]` render that block (F-024): the paragraph or list item ending in `^id`, without it; a missing block shows as a link with the reason. A reading view is rendered again whenever the index changes, so an embedded note's edits appear once indexed.
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

## Block references

A **block** is a paragraph or a list item (F-024; `parser/Transclusion`): its lines run until a blank line, a heading, a fence or the next list marker, and fenced code is never one. A block whose last line ends in whitespace and `^id` (§Markdown dialect) can be linked as `[[Note#^id]]` or `[[Note^id]]` and embedded the same way. The index already keeps each note's IDs (`blocks`) and each link's block (`links.block`).

**Following** a link to a heading or a block, in reading mode, opens the note at that line, found in the open buffer if the note is open, else in the file; with no such heading or block the note opens where it was and the status bar says so.

**Copy Block Link** (`Ctrl+Alt+L`, the File menu, the command palette and the editor's context menu) copies `[[<note>#^<id>]]` for the block under the cursor, the note named as the completer would name it from the vault. A block without an ID is given one first: six lowercase letters and digits, not already used in the note, appended to its last line after a space as one undo step; it is kept when the note is saved. On a heading, a blank line or in code it copies nothing and says why.

## Bookmarks and recent notes

Both are kept per vault and per machine, in `.coeus/` (F-014): a laptop has its own stars and recents, as it has its own index (D-011). Each is written through `AtomicWriter` as JSON (`version`, then the list); a file that cannot be read is treated as empty, and an entry that cannot be read is skipped, never the rest.

**Bookmarks** (`bookmarks.json`, `vault/Bookmarks`) are notes and searches, in the order they were starred. A note is named by its root (`vault` or a linked root's name) and its path within it, so it survives the root moving on disk; a search by its text and mode (`words`, `meaning`, `both`). A note is starred or unstarred by the ☆ in its header or Toggle Bookmark (`Ctrl+Alt+S`); a search by the ☆ beside the Search pane's query, which is filled while that text in that mode is starred. The **Bookmarks** pane, a left-dock tab beside Explorer, Tags, Search and Registers (View › Toggle Bookmarks, `Ctrl+Shift+K`), lists them: `★ <file name>` with the note's display path, or `⌕ <query>` with its mode. Activating a note opens it; a search fills the Search pane in its mode and runs. A note no longer there stays listed, struck through, until removed. The context menu opens, moves up, moves down or removes one; Delete removes the selected one.

**Recent notes** (`recent.json`, `vault/RecentNotes`) are the 30 notes most recently made the current tab, newest first, each once, by root and path; they are written two seconds after they settle and when the window closes. Open Recent Note… (`Ctrl+R`) shows them in a palette, in that order, with the fuzzy filter, except that the note already in front goes last, so Enter opens the one before it; File › Open Recent lists them as a menu. Notes no longer there, or in a root missing on this machine, are left out.

## Themes and shortcuts

Both are per-user preferences, kept in Coeus's settings file (`~/.config/coeus/coeus.ini`), never in a vault (F-027).

**Themes.** View › Theme offers **System** (the default: the desktop's own style and palette), **Light**, **Dark** and **Cassette**, which use Qt's Fusion style with Coeus's palettes so they look the same on any desktop (`ui/Theme`, `appearance/theme`). **Cassette** is cassette futurism as an amber CRT terminal: warm near-black panels, amber phosphor text, burnt-orange accents and rules, square corners, and the fixed-pitch font throughout, set by a style sheet of its own that comes before the user's snippets, so they can override it. Leaving it gives every widget back its inherited font, except those always fixed-pitch (the editor, version and diff views, the commit message). A change applies at once, without a restart, to every widget: each is made to inherit the application's palette again, since a style sheet once applied and removed leaves some widgets deaf to it (BUG-011). Views do not hold colours of their own: the editor's highlighting, the reading view's style sheet, diff lines, lint underlines, the project board's stale reviews and error messages all take them from the theme's semantic colours (accent, error, warning, info, added, removed, highlight, tint), chosen for the theme (Cassette has its own) or for whether the palette in use is dark, and are redrawn when it changes; lists that colour their rows when filled (Problems, Registers, Related, History, Tags) are filled again. Combo boxes filled from the vault are sized to a few words, never to their longest item, so a restyle cannot widen the window (BUG-009).

**Style snippets.** Every `*.qss` file in `~/.config/coeus/snippets/` (`$XDG_CONFIG_HOME/coeus/snippets/`) is applied, in name order, as the application's Qt style sheet, Coeus's counterpart of Obsidian's CSS snippets. They are read when Coeus starts and by View › Theme › Reload Style Snippets, which names the files applied; Open Snippets Folder makes and opens the folder. A file that cannot be read is named and left out.

**Keyboard shortcuts.** Every command registered with the command palette can be given a shortcut, or none, in View › Keyboard Shortcuts… (`ui/ShortcutsDialog`): a filterable list of commands with their shortcut and default; choose one and press keys. A shortcut another command has is refused with that command's name, so no two commands share one; Clear removes one, Reset restores the default, Reset All every default. A choice applies at once and is saved under `[shortcuts]`, keyed by the command's name (`New Note`, `Toggle Blame`); one equal to the default is not saved, so changing a default later reaches everyone who kept it.

## Constants

| Name | Default | Where |
|------|---------|-------|
| `review_threshold_days` | 14 | config.toml `[registers]` |
| `model` | `~/.local/share/coeus/models/bge-small-en-v1.5-f16.gguf` | config.toml `[semantic]` |
| `chunk_tokens` | 384 | config.toml `[semantic]` |
| `chunk_overlap` | 48 | config.toml `[semantic]` |
| `related_count` | 5 | config.toml `[semantic]` |
| `sync_interval_s` | 300 | config.toml `[sync]` |
| `autosave_interval_s` | 5 | config.toml `[editor]` |
| `watcher_debounce_ms` | 200 | config.toml `[vault]` |
| `allow_writes` | false | config.toml `[mcp]` |
| `write_undo_s` | 10 | config.toml `[mcp]` |
| `sidecar_compact_days` | 7; 0 turns timed compaction off | config.toml `[semantic]` |
| `gitignore_offer` | unset; `"declined"` after Don't Ask Again | config.toml `[vault]` |
| `folder` | `daily` | config.toml `[daily]` |
| `format` | `yyyy-MM-dd` | config.toml `[daily]` |
| `template` | none | config.toml `[daily]` |
| `folder` | `templates` | config.toml `[templates]` |
