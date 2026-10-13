#!/usr/bin/env python3
"""Tests for make_demo_vault.py. Run: python3 tools/test_make_demo_vault.py"""

import contextlib
import io
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
