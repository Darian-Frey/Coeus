#!/usr/bin/env python3
"""Tests for make_demo_vault.py. Run: python3 tools/test_make_demo_vault.py"""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import make_demo_vault  # noqa: E402


def run(*args):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return make_demo_vault.main([str(a) for a in args])


class MakeDemoVaultTest(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.target = Path(self._dir.name) / "demo"

    def tearDown(self):
        self._dir.cleanup()

    def test_builds_vault_and_linked_root(self):
        self.assertEqual(run(self.target, "--no-git"), 0)
        vault = self.target / "vault"
        for name in ("Start here.md", "Home.md", "reference/Markdown showcase.md", "ideas/theia.md",
                     ".coeus/config.toml", "attachments/diagram.png", ".secret.md"):
            self.assertTrue((vault / name).exists(), name)
        self.assertTrue((self.target / "pontus/build/_deps/raylib/README.md").exists())
        self.assertIn(str(self.target / "pontus"), (vault / ".coeus/config.toml").read_text())
        self.assertTrue((vault / "attachments/diagram.png").read_bytes().startswith(b"\x89PNG"))

    def test_showcases_the_newer_features(self):
        self.assertEqual(run(self.target, "--no-git"), 0)
        vault = self.target / "vault"
        tour = (vault / "reference/Live preview tour.md").read_text()
        for construct in ("> [!note]", "| Feature", "```mermaid", "sequenceDiagram", "$$", "![[attachments/spec.pdf#page=2]]",
                          "![[Mnemosyne]]", "![[Long note#Section 4]]", "![[Mnemosyne#^cache]]", "^text"):
            self.assertIn(construct, tour)
        self.assertIn("\\int_0^\\infty", tour)  # maths written with single backslashes
        # A real PDF: three pages, each object where the cross-reference table says.
        pdf = (vault / "attachments/spec.pdf").read_bytes()
        self.assertTrue(pdf.startswith(b"%PDF-1.4"))
        self.assertIn(b"/Count 3", pdf)
        xref = int(pdf.rsplit(b"startxref", 1)[1].split()[0])
        self.assertTrue(pdf[xref:].startswith(b"xref"))
        for line in pdf[xref:].split(b"\n")[3:]:
            if line.endswith(b" n "):
                offset = int(line.split()[0])
                self.assertRegex(pdf[offset:offset + 12].decode("latin-1"), r"^\d+ 0 obj")
        # Bookmarks and recents seeded as Coeus keeps them.
        bookmarks = json.loads((vault / ".coeus/bookmarks.json").read_text())["bookmarks"]
        self.assertEqual([b["kind"] for b in bookmarks], ["note", "note", "search", "search"])
        self.assertTrue(all((vault / b["path"]).exists() for b in bookmarks if b["kind"] == "note"))
        recent = json.loads((vault / ".coeus/recent.json").read_text())["recent"]
        self.assertTrue(all((vault / r["path"]).exists() for r in recent))

    def test_never_deletes_a_folder_it_did_not_make(self):
        self.target.mkdir()
        keep = self.target / "precious.txt"
        keep.write_text("mine")
        self.assertEqual(run(self.target, "--force", "--no-git"), 1)
        self.assertTrue(keep.exists())

    def test_force_rebuilds_its_own_demo(self):
        self.assertEqual(run(self.target, "--no-git"), 0)
        (self.target / "vault/Home.md").write_text("edited")
        self.assertEqual(run(self.target, "--no-git"), 1)  # refuses without --force
        self.assertEqual(run(self.target, "--force", "--no-git"), 0)
        self.assertNotEqual((self.target / "vault/Home.md").read_text(), "edited")

    def test_bulk_notes(self):
        self.assertEqual(run(self.target, "--no-git", "--bulk", "50"), 0)
        self.assertEqual(len(list((self.target / "vault/bulk").rglob("*.md"))), 50)


if __name__ == "__main__":
    unittest.main()
