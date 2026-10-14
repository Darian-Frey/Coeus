#!/usr/bin/env python3
"""Builds a demo vault that exercises every Coeus feature built so far.

    python3 tools/make_demo_vault.py [TARGET] [--force] [--bulk N]

TARGET (default ~/CoeusDemo) receives two folders:

    vault/   the vault to open:  ./build/coeus ~/CoeusDemo/vault
    pontus/  a linked project root, listed in vault/.coeus/config.toml

Both are Git repositories, so the first-open .gitignore offer appears and
pontus/build/ is ignored. "Start here.md" in the vault walks through each
feature. --bulk N adds N synthetic notes under vault/bulk/ for performance.

--force rebuilds TARGET, but only if it holds the marker file this script
writes, so it can never delete a folder it did not make. Standard library
only.
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import struct
import subprocess
import sys
import textwrap
import zlib
from pathlib import Path

MARKER = ".coeus-demo"
TODAY = "2026-10-13"


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip("\n"), encoding="utf-8")


def png(width: int, height: int) -> bytes:
    """A small teal gradient PNG, made without any imaging library."""
    rows = b""
    for y in range(height):
        rows += b"\x00" + bytes(
            v for x in range(width) for v in (31 + x // 4, 122 + y // 2, 112 + (x * y) % 40))
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


PDF = b"""%PDF-1.4
1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj
2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj
3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] >> endobj
trailer << /Root 1 0 R >>
%%EOF
"""

SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
  <circle cx="32" cy="32" r="28" fill="#1f7a70"/><circle cx="32" cy="32" r="6" fill="#ffffff"/>
</svg>
"""


def git_init(folder: Path) -> None:
    if shutil.which("git"):
        subprocess.run(["git", "init", "-q", str(folder)], check=False)


def git_history(root: Path) -> None:
    """Two commits of DECISIONS.md in the linked root, for the History section (F-020)."""
    if not shutil.which("git"):
        return
    def git(*args: str, date: str) -> None:
        env = dict(os.environ, GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date)
        subprocess.run(["git", "-C", str(root), "-c", "user.name=Demo Author", "-c", "user.email=demo@example.org",
                        "-c", "commit.gpgsign=false", *args], check=False, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    decisions = root / "DECISIONS.md"
    final = decisions.read_text(encoding="utf-8")
    decisions.write_text(final.replace("Fixed 120 Hz timestep.", "Fixed 60 Hz timestep."), encoding="utf-8")
    git("add", "-A", date="2026-10-01T10:00:00+01:00")
    git("commit", "-q", "-m", "Start Pontus", date="2026-10-01T10:00:00+01:00")
    decisions.write_text(final, encoding="utf-8")
    git("commit", "-q", "-am", "Raise the timestep to 120 Hz\n\nThe contact solver needed it.", date="2026-10-05T16:30:00+01:00")


def build_linked_root(root: Path) -> None:
    write(root / "README.md", """
        > **Status:** Active
        > **Provenance:** Demo data for Coeus
        > **Last reviewed:** 2026-10-01
        > **Why this status:** Linked root in the demo vault.

        # Pontus

        A physics sandbox, here as a **linked root**: indexed with the vault but kept
        out of vault sync. It links back to the vault's [[vault:Home]] and to its own
        [[DECISIONS]]. #project/pontus
        """)
    write(root / "DECISIONS.md", """
        # Decisions

        ### D-001 Fixed timestep
        **Status:** Accepted

        **Context.** The integrator needs determinism; see [[README]] and the
        [[docs/SPEC|spec]].

        **Decision.** Fixed 120 Hz timestep. #physics
        """)
    write(root / "docs/SPEC.md", """
        # Specification

        ## Integrator
        Semi-implicit Euler at 120 Hz. The Memory engine is not involved here.

        ## Units
        SI throughout.
        """)
    write(root / "src/main.cpp", "int main() { return 0; }\n")
    write(root / ".gitignore", "build/\n")
    write(root / "build/_deps/raylib/README.md", "# Third-party docs\n\nShould never be indexed: build/ is ignored.\n")


def build_vault(vault: Path, linked: Path) -> None:
    write(vault / ".coeus/config.toml", f"""
        # Linked project roots (SPEC.md §Linked roots).
        [[roots]]
        name = "pontus"
        path = "{linked}"

        # A root that does not exist here: shown greyed, never emptied.
        [[roots]]
        name = "aether"
        path = "/nonexistent/aether-on-another-machine"

        # Daily notes (F-009) and templates (F-010).
        [daily]
        folder = "daily"
        format = "yyyy-MM-dd"
        template = "templates/Daily.md"

        [templates]
        folder = "templates"
        """)

    write(vault / "Start here.md", f"""
        ---
        tags: [demo, guide]
        updated: {TODAY}
        ---
        # Start here

        This vault exercises everything Coeus does so far. Each section says what to
        try and what you should see. Links: [[Home]], [[Markdown showcase]].

        ## Explorer and linked roots
        - The Explorer shows **vault**, **pontus** (a linked root) and **aether**,
          greyed because its folder does not exist on this machine.
        - `pontus/build/` is in its `.gitignore`: nothing under it is indexed.
        - `.secret.md` and `.obsidian/` are hidden: never listed, never indexed.
        - Attachments in `attachments/` are listed; clicking one says it cannot be
          previewed yet.

        ## First open
        - This vault is a Git repository, so Coeus offers to add `.coeus/` to its
          `.gitignore`. Try **Not Now**, reopen, then **Add**.

        ## Editing, saving, conflicts and recovery
        1. Edit this note and press `Ctrl+S`; the tab's ● disappears.
        2. Edit it again without saving, then change the file outside Coeus
           (`echo "outside" >> "Start here.md"`): a conflict prompt appears.
        3. Close a tab with unsaved changes: Save / Discard / Cancel.
        4. Type something, wait five seconds, kill Coeus (`pkill coeus`), restart:
           it offers to recover the unsaved text.
        5. Edit `daily/2026-10-11.md` outside Coeus while it is open in a clean tab:
           the tab follows within a second.

        ## Reading mode
        - `Ctrl+E` here, then open [[Markdown showcase]] and toggle it: callouts,
          tables, tasks, highlights, images, code that never turns into links.

        ## Links and completion
        - Type `[[READ` in any note: two READMEs, the nearer first; picking one
          writes a path that cannot land on the other.
        - Type `[[Mnemo` and `[[Memory` (an alias), and `[[Mnemosyne#` for headings.
        - In reading mode, [[Shared]] is **ambiguous** (two notes share the title at
          equal distance) and [[Not yet written]] is **unresolved**: both say so.
        - [[pontus:README]] crosses into the linked root.

        ## Sidebar
        - **Backlinks** on [[Mnemosyne]] lists the notes linking to it, and under
          *Unlinked mentions* the ones that only say "Mnemosyne" or "memory engine".
        - **Outline** on [[Long note]] shows nested headings; click to jump, and move
          the cursor to watch the highlight follow.

        ## Tags, search, properties
        - **Tags** tab: `#project` nests `coeus` and `pontus`; `#Idea` and `#idea`
          are one tag. Click one to filter the Explorer.
        - **Search** (`Ctrl+Shift+F`): try `vault cache`, `"plain folder"`,
          `tag:project`, `path:ideas/`, `file:readme`, `re:/D-0\\d\\d/`, `idea -draft`.
        - **Properties**: open `ideas/theia.md` - `status: Done` is flagged, not
          changed; pick *Active*. `ideas/broken frontmatter.md` is read-only.

        ## Daily notes and templates
        - `Ctrl+D` opens today's note, creating it from `templates/Daily.md` if it
          does not exist yet; press it again and the same note opens, untouched.
        - In any note, **Insert Template…** (File menu or `Ctrl+P`) offers the
          `templates/` folder. *Meeting* adds its `tags` to the note's frontmatter
          without touching the keys already there, and its body at the cursor;
          `Ctrl+Z` takes it all back in one step. Inserted into a note under
          `projects/coeus/`, its `{{next:D}}` becomes D-020; elsewhere it stays as
          written.

        ## Registers
        - The **Registers** tab (beside Explorer) lists a project's entries; open
          `projects/coeus/README.md` and it switches to *projects/coeus*. Filter by
          kind or status, and click an entry to jump to it.
        - In `projects/coeus/README.md`, type `D-0`: the project's decisions are
          offered, with titles.
        - **Insert Next ID…** (`Ctrl+Shift+I`) in `projects/coeus/DECISIONS.md`
          appends D-020 with *Title* selected. Choose *Bugs* instead and
          `BUGS.md` is created for it.

        ## Problems
        - With a note in `projects/coeus/` open, the status bar shows the
          project's lint count (3 errors, 3 warnings); click it, or press
          `Ctrl+Shift+M`, for the **Problems** panel. Each is planted:
          D-019 cites F-099, which does not exist, and has no *Reversal
          conditions*; F-018's status *Planned* is not in the vocabulary; F-018
          cites D-003, which is superseded; two citations are not returned.
        - Click a problem: the register opens at its line, the culprit underlined
          with a wavy line; hover it for the message. Change *Planned* to
          *Not started* and save: the error goes.

        ## Git history
        - Open [[pontus:DECISIONS]]: the sidebar's **History** shows the branch
          and two commits. Click the older one: the note as it was (60 Hz) opens
          read-only in its own tab. The vault itself has no commits yet, so its
          notes say so.
        - Still in [[pontus:DECISIONS]], type a line, then **Diff vs HEAD**: the
          unsaved line shows in green. Right-click the newer commit, **Compare
          with the Version Before**: 60 Hz in red, 120 Hz in green.
        - **Blame** (or `Ctrl+Alt+B`): each run of lines names its commit and
          date beside the text; your new line is *Not committed*.
        - The vault is a repository with nothing committed: the status bar says
          how many files are changed. Click it (or `Ctrl+Alt+C`): notes are
          ticked, attachments not. Write a message and **Commit**; History now
          lists it. (Push needs a remote; this demo has none, so leave it off.)

        ## Project board
        - `Ctrl+Shift+J` opens the **Project Board** under the editor: coeus,
          pontus-notes and the pontus root by status, how long since each was
          reviewed (amber past 14 days: pontus-notes, Dormant, was last
          reviewed in August), the phase its
          ROADMAP is in, what its CHANGELOG has not released, and its Git branch;
          below them the ideas by stage. Click a row to open it.
        - Right-click pontus-notes, **Refresh Header…**: after you confirm, its Last
          reviewed becomes today and the amber goes.
        - Right-click the idea Theia, **Promote Idea…**: `projects/Theia/` appears
          with a README (status header first) and empty FEATURES, ROADMAP,
          DECISIONS, CLAUDE and CHANGELOG, and the idea note is gone.

        ## Index
        - File → **Verify Index**; File → **Rebuild Index**.
        """)

    write(vault / "Home.md", """
        ---
        aliases: [Dashboard]
        tags: [hub]
        ---
        # Home

        The hub. Every link form:

        - Plain: [[Mnemosyne]]
        - Alias: [[Mnemosyne|the memory engine]]
        - Heading: [[Long note#Section 12]]
        - Block: [[Markdown showcase#^summary]]
        - Path: [[projects/coeus/README]] and [[projects/pontus-notes/README]]
        - Cross-root: [[pontus:DECISIONS]]
        - Ambiguous: [[Shared]]
        - Unresolved: [[Not yet written]]
        - Markdown link: [the showcase](reference/Markdown%20showcase.md)
        - Embedded image: ![[attachments/diagram.png]]

        Projects: [[projects/coeus/DECISIONS|Coeus decisions]]. #hub
        """)

    write(vault / "projects/coeus/README.md", """
        > **Status:** Active
        > **Provenance:** Demo data for Coeus
        > **Last reviewed:** 2026-10-13
        > **Why this status:** Being built.

        # Coeus

        A local-first knowledge base. The vault holds notes; the index is a cache.
        See [[DECISIONS]] and [[FEATURES]]. #project/coeus
        """)
    write(vault / "projects/coeus/DECISIONS.md", """
        # Decisions

        ### D-003 Index in JSON files
        **Status:** Superseded by D-005

        **Decision.** One JSON file per note beside it.

        **Reversal conditions.** None; superseded.

        ### D-005 Vault is a plain folder
        **Status:** Accepted
        **Related:** F-004

        **Context.** Notes already live in Git and are edited by other tools.

        **Decision.** A plain folder of Markdown and a derived index; deleting the
        index loses nothing. The [[Mnemosyne]] embeddings are a cache too.

        **Reversal conditions.** If a second machine cannot keep the folder in sync.

        ### D-019 Watch with inotify
        **Status:** Accepted
        **Related:** F-004, F-099

        **Decision.** One watch per folder, so in-place edits are seen. #storage/index
        """)
    write(vault / "projects/coeus/FEATURES.md", """
        # Features

        ### F-004 Wikilinks with autocomplete
        **Status:** Complete
        **Related:** D-005

        ### F-018 Semantic search
        **Status:** Planned
        **Related:** D-003
        Uses the memory engine. #project/coeus
        """)
    write(vault / "projects/pontus-notes/README.md", """
        > **Status:** Dormant
        > **Provenance:** Demo data for Coeus
        > **Last reviewed:** 2026-08-20
        > **Why this status:** Waiting on the Pontus root; stale on purpose.

        # Pontus notes

        Personal notes about the [[pontus:README|Pontus]] project. A second README, so
        the completer has two to tell apart. #project/pontus
        """)

    write(vault / "ideas/theia.md", """
        ---
        stage: scoped
        codename: Theia
        status: Done
        repo:
        tags: [Idea, vision]
        updated: 2026-10-01
        priority: 2
        summary: A visual front end for Mnemosyne
        ---
        # Theia

        `status: Done` is outside the project-status vocabulary: the properties panel
        flags it and leaves it alone until you choose.
        """)
    write(vault / "ideas/hermes.md", """
        ---
        stage: idea
        tags: [idea]
        summary: Message routing between the author's tools
        ---
        # Hermes

        Still a draft idea. #draft
        """)
    write(vault / "ideas/broken frontmatter.md", """
        ---
        title: This frontmatter
        tags: [never, closed
        ---
        # Broken frontmatter

        The YAML above is invalid: the body is still indexed (#searchable), and the
        properties panel is read-only with the parser's message.
        """)

    write(vault / "reference/Mnemosyne.md", """
        ---
        aliases: [Memory engine]
        tags: [reference, project/coeus]
        ---
        # Mnemosyne

        The local embedding subsystem.

        ## Interface
        `embed(texts)`, `model_name()`, `dim()`.

        ## Notes
        Chunks are hashed; unchanged chunks are not re-embedded.
        """)
    write(vault / "reference/Markdown showcase.md", """
        ---
        status: Active
        tags: [demo]
        ---
        # Markdown showcase

        **Bold**, *italic*, ~~struck~~, ==highlighted==, `inline code`, and maths
        $E = mc^2$. A soft break follows
        on this line. ^summary

        > [!tip] Callout title
        > Callouts render as boxes in reading mode.

        > [!warning]
        > A callout with no title takes its kind.

        > A plain quote.

        - [x] A finished task
        - [ ] An open task

        1. First
        2. Second

        | Feature | Status |
        |:--------|-------:|
        | Editor  | Done   |
        | Canvas  | Later  |

        ```cpp
        // Inside code nothing is a [[link]] or a #tag.
        int answer = 42;
        ```

        ```mermaid
        graph LR; vault --> index
        ```

        ![Diagram](../attachments/diagram.png) ![[attachments/logo.svg]]

        Raw HTML is shown, not run: <b>not bold</b>. Tags: #demo/showcase, not #42.
        """)
    sections = "\n".join(
        f"{'#' * (2 + (i % 3 == 1))} Section {i}\n\nParagraph {i} of a long note.\n" for i in range(1, 31))
    write(vault / "reference/Long note.md", "# Long note\n\nFor the outline panel.\n\n" + sections)
    write(vault / "reference/Shared.md", "# Shared (reference)\n\nOne of two notes titled Shared.\n")
    write(vault / "archive/Shared.md", "# Shared (archive)\n\nThe other note titled Shared.\n")

    # Past days only: Ctrl+D then creates today's note from the template.
    write(vault / "daily/2026-10-11.md", """
        ---
        tags: [daily]
        ---
        # 2026-10-11

        Talked about Mnemosyne without linking it. The memory engine needs a model
        choice (D-008). Reviewed the vault cache decision. #daily
        """)
    write(vault / "daily/2026-10-12.md", """
        # 2026-10-12

        First run of the indexer on the project folders. #daily #Idea
        """)

    write(vault / "templates/Daily.md", """
        ---
        tags: [daily]
        ---
        # {{title}}

        Started at {{time}} on {{date:dddd d MMMM yyyy}}.

        ## Notes

        ## Tomorrow
        """)
    write(vault / "templates/Meeting.md", """
        ---
        tags: [meeting]
        attendees: []
        ---
        ## {{title}} meeting, {{date}}

        - Agenda:
        - Decisions: {{next:D}}
        - Actions:
        """)

    write(vault / ".secret.md", "# Hidden\n\nNever listed or indexed.\n")
    write(vault / ".obsidian/workspace.json", "{}\n")
    (vault / "attachments").mkdir(parents=True, exist_ok=True)
    (vault / "attachments/diagram.png").write_bytes(png(240, 90))
    (vault / "attachments/spec.pdf").write_bytes(PDF)
    (vault / "attachments/logo.svg").write_text(SVG, encoding="utf-8")


def build_bulk(vault: Path, count: int) -> None:
    rng = random.Random(20261013)
    words = ["vault", "index", "note", "link", "cache", "folder", "search", "tag", "project", "idea",
             "memory", "engine", "outline", "heading", "editor", "reading", "property", "status"]
    for n in range(count):
        body = " ".join(rng.choice(words) for _ in range(120))
        links = " ".join(f"[[bulk note {rng.randrange(count)}]]" for _ in range(3))
        write(vault / f"bulk/folder {n % 40:02d}/bulk note {n}.md",
              f"# Bulk note {n}\n\n{body}\n\n{links} #bulk/group{n % 5}\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("target", nargs="?", default=Path.home() / "CoeusDemo", type=Path)
    parser.add_argument("--force", action="store_true", help="rebuild a demo made by this script")
    parser.add_argument("--bulk", type=int, default=0, metavar="N", help="add N synthetic notes")
    parser.add_argument("--no-git", action="store_true", help="do not make the folders Git repositories")
    args = parser.parse_args(argv)

    target: Path = args.target.expanduser().resolve()
    if target.exists() and any(target.iterdir()):
        if not (target / MARKER).exists():
            print(f"{target} exists and was not made by this script; choose another folder.", file=sys.stderr)
            return 1
        if not args.force:
            print(f"{target} already holds a demo; use --force to rebuild it.", file=sys.stderr)
            return 1
        shutil.rmtree(target)

    vault, linked = target / "vault", target / "pontus"
    target.mkdir(parents=True, exist_ok=True)
    (target / MARKER).write_text("Made by tools/make_demo_vault.py; --force may delete this folder.\n")
    build_linked_root(linked)
    build_vault(vault, linked)
    if args.bulk:
        build_bulk(vault, args.bulk)
    if not args.no_git:
        git_init(vault)
        git_init(linked)
        git_history(linked)

    notes = sum(1 for p in vault.rglob("*.md") if not any(part.startswith(".") for part in p.relative_to(vault).parts))
    print(f"Demo vault: {vault} ({notes} notes)\nLinked root: {linked}\nOpen it with: ./build/coeus {vault}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
