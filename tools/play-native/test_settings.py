#!/usr/bin/env python3
"""Tests of settings.env, the built-in mods switched off in the installer (Built-in Mods...), in both editions:
the Mac launcher's reader (settings_env in tools/play-native/launch.sh, run with macOS's /bin/bash) and Learn &
Play's (win-x86/app/lp_settings.c, compiled here) read the same made-up files and must agree on what is set and
what is logged; and the keys in the two readers and the installer (tools/installer/RSReARMedInstaller.swift)
are the same list.

    python3 tools/play-native/test_settings.py
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LAUNCH = os.path.join(HERE, "launch.sh")
LP_DIR = os.path.join(ROOT, "win-x86", "app")
INSTALLER = os.path.join(ROOT, "tools", "installer", "RSReARMedInstaller.swift")

# What the C reader is run with: apply, then what each key is now, then the log (as the Bash one below).
HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "lp_settings.h"
int main(int argc, char **argv) {
  (void)argc;
  lp_settings_apply(argv[1]);
  for (int i = 0; lp_settings_keys[i]; i++) {
    const char *v = getenv(lp_settings_keys[i]);
    if (v && *v) printf("%s=%s\n", lp_settings_keys[i], v);
  }
  lp_settings_log(argv[1], stdout);
  const char *g = getenv("RS_NETGUARD");
  printf("RS_NETGUARD=%s\n", g ? g : "unset");
  return 0;
}
"""
BASH = r"""
set -u
eval "$SNIPPET"
settings_env "$1"
for k in "${settings_keys[@]}"; do [[ -n ${!k:-} ]] && echo "$k=${!k}"; done
[[ ${#settings_log[@]} == 0 ]] || printf '%s\n' "${settings_log[@]}"
echo "RS_NETGUARD=${RS_NETGUARD-unset}"
"""


def read(path):
    with open(path) as f:
        return f.read()


def launch_snippet():
    """launch.sh's settings_keys and settings_env, as written there."""
    src = read(LAUNCH)
    keys = re.search(r"^settings_keys=\(.*\)$", src, re.M)
    fn = re.search(r"^settings_env\(\) \{.*?^\}$", src, re.M | re.S)
    assert keys and fn, "launch.sh has no settings_keys or settings_env"
    return keys.group(0) + "\n" + fn.group(0) + "\n"


def launch_keys():
    return re.search(r"^settings_keys=\((.*)\)$", read(LAUNCH), re.M).group(1).split()


def lp_keys():
    src = read(os.path.join(LP_DIR, "lp_settings.c"))
    body = re.search(r"lp_settings_keys\[\] = \{(.*?)\};", src, re.S).group(1)
    return re.findall(r'"([A-Z0-9_]+)"', body)


def installer_keys():
    return re.findall(r'BuiltInMod\(key: "([A-Z0-9_]+)"', read(INSTALLER))


class Keys(unittest.TestCase):
    def test_same_list_everywhere(self):
        self.assertTrue(launch_keys())
        self.assertEqual(launch_keys(), lp_keys())
        self.assertEqual(launch_keys(), installer_keys())

    def test_nothing_that_weakens_offline(self):
        # The network guard and the Steam layer's switches stay out of a file anyone can write.
        for k in launch_keys():
            self.assertTrue(k.startswith("RS_MODS_") or k == "RS_REARMED", k)
            self.assertNotIn(k, ("RS_MODS_PATH", "RS_MODS_LOG"))


class Readers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="rs-settings-test-")
        cls.lp = os.path.join(cls.tmp, "lp_settings_test")
        with open(os.path.join(cls.tmp, "harness.c"), "w") as f:
            f.write(HARNESS)
        cc = shutil.which("cc") or shutil.which("clang")
        cls.cc_error = None
        if not cc:
            cls.cc_error = "no C compiler"
            return
        r = subprocess.run([cc, "-Wall", "-Werror", "-I", LP_DIR, os.path.join(cls.tmp, "harness.c"),
                            os.path.join(LP_DIR, "lp_settings.c"), "-o", cls.lp], capture_output=True, text=True)
        if r.returncode:
            cls.cc_error = r.stderr

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_both(self, content, env=None):
        """Both readers on a file with CONTENT (None: no file), with ENV; their outputs, which must agree."""
        path = os.path.join(self.tmp, "settings.env")
        if os.path.exists(path):
            os.unlink(path)
        if content is not None:
            with open(path, "w", newline="") as f:
                f.write(content)
        base = {"PATH": "/usr/bin:/bin", "HOME": self.tmp}
        base.update(env or {})
        mac = subprocess.run(["/bin/bash", "-c", BASH, "test", path], capture_output=True, text=True,
                             env=dict(base, SNIPPET=launch_snippet()))
        self.assertEqual(mac.returncode, 0, mac.stderr)
        self.assertEqual(mac.stderr, "")
        if self.cc_error:
            self.fail("lp_settings.c did not compile: " + self.cc_error)
        lp = subprocess.run([self.lp, path], capture_output=True, text=True, env=base)
        self.assertEqual(lp.returncode, 0, lp.stderr)
        self.assertEqual(mac.stdout, lp.stdout, "the Mac and Learn & Play readers disagree")
        return mac.stdout.splitlines()

    def test_no_file(self):
        self.assertEqual(self.run_both(None), ["RS_NETGUARD=unset"])

    def test_installer_file(self):
        out = self.run_both("# Written by the installer (Built-in Mods\u2026)\n# more words\n\nRS_MODS_INTRO=0\nRS_REARMED=0\n")
        self.assertEqual(out, ["RS_MODS_INTRO=0", "RS_REARMED=0",
                               "[settings] RS_MODS_INTRO=0", "[settings] RS_REARMED=0", "RS_NETGUARD=unset"])

    def test_spaces_crlf_and_no_last_newline(self):
        out = self.run_both("  RS_MODS_BYPASS = 0 \r\n\tRS_MODS_AUTOSAVE=0")
        self.assertEqual(out, ["RS_MODS_BYPASS=0", "RS_MODS_AUTOSAVE=0",
                               "[settings] RS_MODS_BYPASS=0", "[settings] RS_MODS_AUTOSAVE=0", "RS_NETGUARD=unset"])

    def test_other_lines_ignored(self):
        out = self.run_both("RS_NETGUARD=log\nRS_STEAM_KEEP_CREDENTIALS=1\nRS_MODS_PATH=/tmp\nRS_MODS_INTRO=2\n"
                            "RS_MODS_INTRO\nhello world\nRS_MODS_FIXES=0\n")
        self.assertEqual(out, ["RS_MODS_FIXES=0",
                               "[settings] ignored: RS_NETGUARD=log",
                               "[settings] ignored: RS_STEAM_KEEP_CREDENTIALS=1",
                               "[settings] ignored: RS_MODS_PATH=/tmp",
                               "[settings] ignored: RS_MODS_INTRO=2",
                               "[settings] ignored: RS_MODS_INTRO",
                               "[settings] ignored: helloworld",
                               "[settings] RS_MODS_FIXES=0",
                               "RS_NETGUARD=unset"])

    def test_environment_wins(self):
        out = self.run_both("RS_MODS_INTRO=0\nRS_MODS_OVERLAY=0\n", {"RS_MODS_INTRO": "1", "RS_MODS_OVERLAY": "0"})
        self.assertEqual(out, ["RS_MODS_INTRO=1", "RS_MODS_OVERLAY=0",
                               "[settings] RS_MODS_INTRO=0 not used: the environment has RS_MODS_INTRO=1",
                               "[settings] RS_MODS_OVERLAY=0", "RS_NETGUARD=unset"])

    def test_empty_environment_value_is_unset(self):
        out = self.run_both("RS_MODS_MULTIPLAYER=0\n", {"RS_MODS_MULTIPLAYER": ""})
        self.assertEqual(out, ["RS_MODS_MULTIPLAYER=0", "[settings] RS_MODS_MULTIPLAYER=0", "RS_NETGUARD=unset"])

    def test_first_of_a_repeated_key_counts(self):
        out = self.run_both("RS_MODS_INTRO=0\nRS_MODS_INTRO=1\n")
        self.assertEqual(out, ["RS_MODS_INTRO=0", "[settings] RS_MODS_INTRO=0",
                               "[settings] ignored: RS_MODS_INTRO=1 (repeated)", "RS_NETGUARD=unset"])

    def test_on_is_allowed(self):
        self.assertEqual(self.run_both("RS_MODS_BYPASS=1\n"),
                         ["RS_MODS_BYPASS=1", "[settings] RS_MODS_BYPASS=1", "RS_NETGUARD=unset"])


if __name__ == "__main__":
    unittest.main()
