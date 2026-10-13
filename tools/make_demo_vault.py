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

        ### D-005 Vault is a plain folder
        **Status:** Accepted

        **Context.** Notes already live in Git and are edited by other tools.

        **Decision.** A plain folder of Markdown and a derived index; deleting the
        index loses nothing. The [[Mnemosyne]] embeddings are a cache too.

        ### D-019 Watch with inotify
        **Status:** Accepted

        **Decision.** One watch per folder, so in-place edits are seen. #storage/index
        """)
    write(vault / "projects/coeus/FEATURES.md", """
        # Features

        ### F-004 Wikilinks with autocomplete
        **Status:** Complete

        ### F-018 Semantic search
        **Status:** Not started
        Uses the memory engine. #project/coeus
        """)
    write(vault / "projects/pontus-notes/README.md", """
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

    notes = sum(1 for p in vault.rglob("*.md") if not any(part.startswith(".") for part in p.relative_to(vault).parts))
    print(f"Demo vault: {vault} ({notes} notes)\nLinked root: {linked}\nOpen it with: ./build/coeus {vault}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
