#!/usr/bin/env python3
"""Tests for check_xrefs.py. Run: python3 tools/test_check_xrefs.py"""

import contextlib
import io
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_xrefs  # noqa: E402


class CheckXrefsTest(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)

    def tearDown(self):
        self._dir.cleanup()

    def write(self, name, text):
        (self.root / name).write_text(textwrap.dedent(text), encoding="utf-8")

    def run_check(self):
        return check_xrefs.check(self.root, check_xrefs.documents_in(self.root))

    def messages(self, level):
        return [f.message for f in self.run_check() if f.level == level]

    def test_clean_bidirectional_pair(self):
        self.write("DECISIONS.md", """\
            ### D-001 Choose
            **Related:** AV-001
            """)
        self.write("ATTACK_VECTORS.md", """\
            ### AV-001 Risk
            **Related decisions.** D-001.
            """)
        self.assertEqual(self.run_check(), [])

    def test_one_way_reference_is_a_warning(self):
        self.write("DECISIONS.md", "### D-001 Choose\n**Related:** AV-001\n")
        self.write("ATTACK_VECTORS.md", "### AV-001 Risk\n**Description.** Bad.\n")
        self.assertEqual(self.messages("warning"), ["D-001 cites AV-001, but AV-001 does not cite D-001"])
        self.assertEqual(self.messages("error"), [])

    def test_pairs_outside_the_rules_are_not_reported(self):
        # D cites D: the standard does not require a back-reference.
        self.write("DECISIONS.md", "### D-001 One\n\n### D-002 Two\n**Related:** D-001\n")
        self.assertEqual(self.run_check(), [])

    def test_undefined_reference_is_an_error_anywhere(self):
        self.write("DECISIONS.md", "### D-001 One\n")
        self.write("README.md", "See D-001 and D-099.\n")
        self.assertEqual(self.messages("error"), ["D-099 is cited but not defined"])

    def test_duplicate_definition_is_an_error(self):
        self.write("BUGS.md", "### BUG-001: First\n\n### BUG-001: Again\n")
        self.assertEqual(len(self.messages("error")), 1)
        self.assertIn("already defined", self.messages("error")[0])

    def test_fenced_code_is_ignored(self):
        self.write("SPEC.md", "```\nident TEXT -- e.g. 'AV-999'\n```\n")
        self.assertEqual(self.run_check(), [])

    def test_entry_ends_at_next_heading(self):
        self.write("DECISIONS.md", "### D-001 One\n\n## Notes\nAV-001 here is not D-001's.\n")
        self.write("ATTACK_VECTORS.md", "### AV-001 Risk\n")
        self.assertEqual(self.messages("warning"), [])

    def test_ids_are_normalised_and_bounded(self):
        self.write("FEATURES.md", "### F-001 Thing\nSee D-0001, not XD-001 or D-001a.\n")
        self.write("DECISIONS.md", "### D-001 One\n**Related:** F-001\n")
        self.assertEqual(self.run_check(), [])

    def test_bug_requires_backlink_from_imp(self):
        self.write("BUGS.md", "### BUG-001: Broken\n**Notes.** See IMP-001.\n")
        self.write("IMPROVEMENTS.md", "### IMP-001: Better\n**Notes.** Nothing.\n")
        self.assertEqual(self.messages("warning"), ["BUG-001 cites IMP-001, but IMP-001 does not cite BUG-001"])

    def test_exit_status(self):
        self.write("DECISIONS.md", "### D-001 Choose\n**Related:** AV-001\n")
        self.write("ATTACK_VECTORS.md", "### AV-001 Risk\n")
        root = str(self.root)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(check_xrefs.main([root, "--quiet"]), 0)
            self.assertEqual(check_xrefs.main([root, "--quiet", "--strict"]), 1)
            self.write("README.md", "D-404\n")
            self.assertEqual(check_xrefs.main([root, "--quiet"]), 1)


if __name__ == "__main__":
    unittest.main()
