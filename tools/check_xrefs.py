#!/usr/bin/env python3
"""Cross-reference checker for the scaffold documents (IMP-001).

Walks the project's Markdown documents, builds the graph of F-/C-/D-/AV-/
BUG-/IMP- references and reports:

  error    an ID is cited but no entry defines it (SPEC.md §Lint L1)
  error    an ID is defined by more than one entry (AV-003)
  warning  an entry cites another but is not cited back, for the pairs
           the documentation standard requires (Maintenance Rule 6, and
           rule 4 of the BUGS and IMPROVEMENTS sections; SPEC.md §Lint L4)

An entry "cites" an ID when the ID appears anywhere in its text, because
FEATURES, BUGS and IMPROVEMENTS entries carry references in prose rather
than a Related field. Fenced code blocks are ignored.

Exit status is 1 if there are errors, or warnings with --strict; else 0.
Replaced by `coeus --lint` once F-016 exists.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REGISTER_FILES = (
    "FEATURES.md",
    "CLAIMS.md",
    "DECISIONS.md",
    "ATTACK_VECTORS.md",
    "BUGS.md",
    "IMPROVEMENTS.md",
)

ENTRY_HEADING = re.compile(r"^### (F|C|D|AV|BUG|IMP)-(\d{3,}):? (.+)$")
SECTION_HEADING = re.compile(r"^#{1,3} ")
REFERENCE = re.compile(r"(?<![\w-])(F|C|D|AV|BUG|IMP)-(\d{3,})(?![\w])")
FENCE = re.compile(r"^\s*(```|~~~)")

# Citer kind -> kinds that must cite it back when cited by it.
BACKLINK_RULES: dict[str, set[str]] = {
    "D": {"AV", "F", "C"},
    "AV": {"D", "C"},
    "F": {"D"},
    "C": {"D", "AV"},
    "BUG": {"AV", "D", "IMP", "BUG"},
    "IMP": {"BUG", "D", "IMP"},
}


@dataclass
class Entry:
    ident: str
    path: Path
    line: int
    cites: dict[str, int] = field(default_factory=dict)  # ident -> first line


@dataclass
class Finding:
    level: str
    path: Path
    line: int
    message: str

    def format(self, root: Path) -> str:
        return f"{self.path.relative_to(root)}:{self.line}: {self.level}: {self.message}"


def kind_of(ident: str) -> str:
    return ident.split("-", 1)[0]


def normalise(kind: str, number: str) -> str:
    return f"{kind}-{int(number):03d}"


def scan(path: Path, entries: list[Entry], mentions: list[tuple[str, Path, int]]) -> None:
    """Collects entries (register files only) and every ID mention."""
    is_register = path.name in REGISTER_FILES
    current: Entry | None = None
    in_fence = False

    for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if FENCE.match(text):
            in_fence = not in_fence
            continue
        if in_fence:
            continue

        heading = ENTRY_HEADING.match(text) if is_register else None
        if heading:
            current = Entry(normalise(heading[1], heading[2]), path, number)
            entries.append(current)
            text = heading[3]  # the title may cite other entries
        elif SECTION_HEADING.match(text):
            current = None

        for ref in REFERENCE.finditer(text):
            ident = normalise(ref[1], ref[2])
            mentions.append((ident, path, number))
            if current is not None and ident != current.ident:
                current.cites.setdefault(ident, number)


def check(root: Path, documents: list[Path]) -> list[Finding]:
    entries: list[Entry] = []
    mentions: list[tuple[str, Path, int]] = []
    for path in documents:
        scan(path, entries, mentions)

    findings: list[Finding] = []
    by_ident: dict[str, Entry] = {}
    for entry in entries:
        if entry.ident in by_ident:
            first = by_ident[entry.ident]
            findings.append(Finding("error", entry.path, entry.line,
                                    f"{entry.ident} is already defined at "
                                    f"{first.path.relative_to(root)}:{first.line}"))
        else:
            by_ident[entry.ident] = entry

    for ident, path, line in mentions:
        if ident not in by_ident:
            findings.append(Finding("error", path, line, f"{ident} is cited but not defined"))

    for entry in by_ident.values():
        required_back = BACKLINK_RULES.get(kind_of(entry.ident), set())
        for target_ident, line in entry.cites.items():
            target = by_ident.get(target_ident)
            if target is None or kind_of(target_ident) not in required_back:
                continue
            if entry.ident not in target.cites:
                findings.append(Finding("warning", entry.path, line,
                                        f"{entry.ident} cites {target_ident}, "
                                        f"but {target_ident} does not cite {entry.ident}"))

    findings.sort(key=lambda f: (str(f.path), f.line, f.message))
    return findings


def documents_in(root: Path) -> list[Path]:
    paths = sorted(root.glob("*.md"))
    docs = root / "docs"
    if docs.is_dir():
        paths += sorted(docs.rglob("*.md"))
    return paths


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=Path(__file__).resolve().parent.parent, type=Path,
                        help="project root (default: the repository containing this script)")
    parser.add_argument("--strict", action="store_true", help="exit 1 on warnings as well as errors")
    parser.add_argument("--quiet", action="store_true", help="print errors only")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    findings = check(root, documents_in(root))
    errors = sum(f.level == "error" for f in findings)
    warnings = len(findings) - errors

    for finding in findings:
        if finding.level == "error" or not args.quiet:
            print(finding.format(root))
    if findings:
        print(f"check_xrefs: {errors} error(s), {warnings} one-way reference(s)", file=sys.stderr)

    return 1 if errors or (args.strict and warnings) else 0


if __name__ == "__main__":
    sys.exit(main())
