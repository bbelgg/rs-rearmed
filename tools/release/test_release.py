#!/usr/bin/env python3
"""Tests of the release tools: scope.py's rules and bump.py's edits, on made-up trees. No game, no network, no git
remote."""
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bump   # noqa: E402
import scope  # noqa: E402


class Scope(unittest.TestCase):
    def test_words_need_nothing(self):
        s = scope.scope(["README.md", "docs/mac/PLAN.md", "docs/release/next.md", "win-x86/README.md", "VERSION"])
        self.assertEqual((s["editions"], s["build"], s["run"], s["suites"], s["unknown"]), ([], [], [], [], []))
        self.assertFalse(s["installer"])

    def test_a_readme_beside_code_needs_nothing(self):
        self.assertIs(scope.rule_for("tools/targets/README.md"), scope.NOTHING)
        self.assertIs(scope.rule_for("shims/steam/README.md"), scope.NOTHING)

    def test_learn_and_play_alone(self):
        s = scope.scope(["win-x86/shims/win32_net.c"])
        self.assertEqual((s["editions"], s["build"], s["run"]), (["lp"], ["lp"], ["lp"]))
        self.assertEqual(s["suites"], ["lp.shims"])

    def test_the_mac_alone(self):
        s = scope.scope(["shims/steam/steam_api.c"])
        self.assertEqual((s["editions"], s["build"], s["run"], s["suites"]), (["mac"], ["mac"], ["mac"], ["steam"]))

    def test_shared_code_reaches_both(self):
        for path in ("shims/opengl/metal/mtl_draw.m", "tools/ws7/recomp/runtime/rc_mods.c",
                     "tools/targets/names/hooks.map", "shims/coreaudio/audio_devs.c"):
            s = scope.scope([path])
            self.assertEqual((s["build"], s["run"]), (["mac", "lp"], ["mac", "lp"]), path)

    def test_the_longest_prefix_decides(self):
        self.assertEqual(scope.scope(["tools/targets/win-x86/edits.map"])["run"], ["lp"])
        self.assertEqual(scope.scope(["tools/ws7/recomp/app/build.sh"])["build"], ["mac"])
        self.assertEqual(scope.scope(["win-x86/install/lp_install.py"])["build"], [])

    def test_the_installer_needs_no_game(self):
        s = scope.scope(["tools/installer/RSReARMedInstaller.swift"])
        self.assertEqual((s["build"], s["run"], s["suites"]), ([], [], []))
        self.assertTrue(s["installer"])

    def test_a_file_no_rule_knows_is_everything(self):
        s = scope.scope(["somewhere/new.c"])
        self.assertEqual(s["unknown"], ["somewhere/new.c"])
        self.assertEqual(s["suites"], list(scope.SUITES))
        self.assertEqual(s["run"], ["mac", "lp"])

    def test_every_rule_names_real_suites(self):
        for prefix, r in scope.RULES.items():
            for s in r["suites"]:
                self.assertIn(s, scope.SUITES, prefix)
            self.assertLessEqual(set(r["build"]) | set(r["run"]), set(r["editions"]) | {"mac", "lp"}, prefix)

    def test_every_file_of_the_repository_has_a_rule(self):
        try:
            files = [p for p in scope.git("ls-files").split("\n") if p]
        except Exception:
            self.skipTest("not a git checkout")
        self.assertEqual([p for p in files if scope.rule_for(p) is None], [])


NEXT = """# The next release: what's new

<!-- how to
write these -->

- **A thing.** It is new.
- Another.
"""
KNOWN = """# Known issues

<!-- note -->

**Both editions**

- No online features.
"""
INSTALL = """# Install

<!-- note -->

It downloads the v{version} source. Works once installed by 1.11.0 or later.
"""
TEMPLATE = "**RS Re-ARMed version**\n<!-- e.g. 1.12.0. Finder's Get Info on the app shows it. -->\n"


class Bump(unittest.TestCase):
    def tree(self, **over):
        root = tempfile.mkdtemp()
        files = {
            "VERSION": "1.12.0\n",
            bump.TEMPLATE: TEMPLATE,
            "docs/mac/PLAN.md": "- **A** (new in 1.12.0): x\n- **B** (after 1.12.0, not released): y\n- **C** (not released)\n",
            "docs/windows/PLAN.md": "- **A** (new in 1.12.0): x\n",
            bump.NEXT: NEXT, bump.KNOWN: KNOWN, bump.INSTALL: INSTALL,
        }
        files.update(over)
        for rel, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
            with open(os.path.join(root, rel), "w") as f:
                f.write(text)
        return root

    def read(self, root, rel):
        return bump.read(root, rel)

    def test_the_edits(self):
        root = self.tree()
        out = os.path.join(root, "out")
        bump.bump(root, "1.13.0", out)
        self.assertEqual(self.read(root, "VERSION"), "1.13.0\n")
        self.assertIn("<!-- e.g. 1.13.0. Finder's", self.read(root, bump.TEMPLATE))
        self.assertEqual(self.read(root, "docs/mac/PLAN.md"),
                         "- **A** (new in 1.12.0): x\n- **B** (new in 1.13.0): y\n- **C** (new in 1.13.0)\n")
        self.assertEqual(self.read(root, "docs/windows/PLAN.md"), "- **A** (new in 1.12.0): x\n")

    def test_the_notes(self):
        root = self.tree()
        out = os.path.join(root, "out")
        bump.bump(root, "1.13.0", out)
        notes = self.read(out, "notes-1.13.0.md")
        self.assertEqual(notes, "## What's new\n\n- **A thing.** It is new.\n- Another.\n\n## Known issues\n\n"
                                "**Both editions**\n\n- No online features.\n\n## Install\n\n"
                                "It downloads the v1.13.0 source. Works once installed by 1.11.0 or later.\n")
        self.assertNotIn("<!--", notes)

    def test_next_is_emptied_and_keeps_its_instructions(self):
        root = self.tree()
        bump.bump(root, "1.13.0", os.path.join(root, "out"))
        self.assertEqual(self.read(root, bump.NEXT), "# The next release: what's new\n\n<!-- how to\nwrite these -->\n")
        with self.assertRaises(SystemExit):  # and so a second release has nothing to say yet
            bump.bump(root, "1.14.0", os.path.join(root, "out"))

    def test_a_dry_run_writes_nothing(self):
        root = self.tree()
        out = os.path.join(root, "out")
        said = bump.bump(root, "1.13.0", out, dry=True)
        self.assertEqual(self.read(root, "VERSION"), "1.12.0\n")
        self.assertFalse(os.path.exists(out))
        self.assertIn("VERSION: 1.12.0 -> 1.13.0", said)

    def test_what_is_refused(self):
        root = self.tree()
        for v in ("1.12.0", "1.11.9", "1.13", "v1.13.0"):
            with self.assertRaises(SystemExit):
                bump.bump(root, v, os.path.join(root, "out"))
        with self.assertRaises(SystemExit):
            bump.bump(self.tree(**{bump.TEMPLATE: "no version line\n"}), "1.13.0", os.path.join(root, "out"))
        self.assertEqual(self.read(root, "VERSION"), "1.12.0\n")

    def test_the_commit_message_says_what_was_checked(self):
        root = self.tree()
        out = os.path.join(root, "out")
        os.makedirs(out)
        with open(os.path.join(out, "checks.json"), "w") as f:
            json.dump({"since": "v1.12.0", "results": [
                {"suite": "hooks", "what": "hook points", "status": "pass"},
                {"suite": "decrypt", "what": "decryption", "status": "pass", "note": "1 skipped inside"},
                {"suite": "ct.cg", "what": "Cg", "status": "skip", "note": "no CMake build"}]}, f)
        bump.bump(root, "1.13.0", out)
        msg = " ".join(self.read(out, "commit-1.13.0.txt").split())
        self.assertTrue(msg.startswith("Release 1.13.0: "))
        self.assertIn("Since 1.12.0:", msg)
        self.assertIn("by what changed since v1.12.0: hook points, decryption (1 skipped inside).", msg)
        self.assertIn("Skipped: Cg (no CMake build).", msg)


if __name__ == "__main__":
    unittest.main()
