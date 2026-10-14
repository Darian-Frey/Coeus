#!/usr/bin/env python3
"""Tests for fetch_model.py, with local file:// URLs instead of the network."""

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fetch_model  # noqa: E402


class FetchModelTest(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        self.source = self.root / "source.gguf"
        self.source.write_bytes(b"GGUF pretend weights")
        self.sha = hashlib.sha256(b"GGUF pretend weights").hexdigest()
        self.dest = self.root / "models"

    def tearDown(self):
        self._dir.cleanup()

    def run_fetch(self, sha):
        return fetch_model.main(["--dest", str(self.dest), "--url", self.source.as_uri(), "--sha256", sha, "--quiet"])

    def test_installs_a_verified_file(self):
        self.assertEqual(self.run_fetch(self.sha), 0)
        self.assertEqual((self.dest / fetch_model.NAME).read_bytes(), b"GGUF pretend weights")
        self.assertEqual(self.run_fetch(self.sha), 0)  # already there: nothing to do

    def test_a_wrong_checksum_installs_nothing(self):
        self.assertEqual(self.run_fetch("0" * 64), 1)
        self.assertFalse((self.dest / fetch_model.NAME).exists())
        self.assertEqual([p.name for p in self.dest.iterdir()], [])  # no partial file left

    def test_pinned_values_look_right(self):
        self.assertIn(fetch_model.REVISION, fetch_model.URL)
        self.assertEqual(len(fetch_model.SHA256), 64)
        self.assertTrue(fetch_model.URL.startswith("https://"))


if __name__ == "__main__":
    unittest.main()
