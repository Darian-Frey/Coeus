#!/usr/bin/env python3
"""Download Coeus's embedding model for semantic search (D-008, F-018).

The model, BAAI's bge-small-en-v1.5 as a GGUF file, is about 67 MB and lives
outside the repository: GitHub refuses files over 100 MB, and weights do not
belong in source control. This script fetches the file pinned below, checks
its SHA-256, and only then moves it into place, so a half-downloaded or
altered file is never used.

    python3 tools/fetch_model.py              # into ~/.local/share/coeus/models/
    python3 tools/fetch_model.py --dest DIR   # somewhere else ([semantic] model in config.toml points there)

Standard library only; run it once per machine.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys
import tempfile
import urllib.request
from pathlib import Path

NAME = "bge-small-en-v1.5-f16.gguf"
# CompendiumLabs' conversion of BAAI/bge-small-en-v1.5 (MIT), pinned to a revision.
REVISION = "d32f8c040ea3b516330eeb75b72bcc2d3a780ab7"
URL = f"https://huggingface.co/CompendiumLabs/bge-small-en-v1.5-gguf/resolve/{REVISION}/{NAME}"
SHA256 = "f0b2fef971e8366438bfd2d9aefea1b0115919389448806d290237f638bae999"
SIZE = 67308128


def default_dest() -> Path:
    data = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(data) / "coeus" / "models"


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(url: str, target: Path, expected: str, quiet: bool = False) -> int:
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and sha256_of(target) == expected:
        if not quiet:
            print(f"Already present and verified: {target}")
        return 0
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".download-", delete=False) as temporary:
        partial = Path(temporary.name)
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                total = int(response.headers.get("Content-Length") or 0)
                done = 0
                for block in iter(lambda: response.read(1 << 20), b""):
                    temporary.write(block)
                    done += len(block)
                    if not quiet and total:
                        print(f"\r{done * 100 // total:3d}% of {total // (1 << 20)} MB", end="", flush=True)
            if not quiet:
                print()
            temporary.flush()
            os.fsync(temporary.fileno())
        except Exception as error:  # network, disk: leave nothing behind
            partial.unlink(missing_ok=True)
            print(f"fetch_model: download failed: {error}", file=sys.stderr)
            return 1
    actual = sha256_of(partial)
    if actual != expected:
        partial.unlink(missing_ok=True)
        print(f"fetch_model: checksum mismatch (got {actual}, expected {expected}); nothing was installed",
              file=sys.stderr)
        return 1
    partial.chmod(0o644)
    partial.replace(target)
    if not quiet:
        print(f"Installed {target}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dest", type=Path, default=default_dest(), help="folder to put the model in")
    parser.add_argument("--url", default=URL, help=argparse.SUPPRESS)        # tests use file:// URLs
    parser.add_argument("--sha256", default=SHA256, help=argparse.SUPPRESS)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    return fetch(args.url, args.dest / NAME, args.sha256, args.quiet)


if __name__ == "__main__":
    sys.exit(main())
