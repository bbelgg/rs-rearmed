#!/usr/bin/env python3
"""Tests of settings.env, the built-in mods switched off in the installer (Built-in Mods…), and options.env,
every other switch the installer's sheet offers, in both editions: the Mac launcher's readers (settings_env
and options_env in tools/play-native/launch.sh, run with macOS's /bin/bash) and Learn & Play's
(win-x86/app/lp_settings.c, compiled here) read the same made-up files and must agree on what is set and
what is logged; and the keys in the two readers and the installer (tools/installer/RSReARMedInstaller.swift)
are the same lists.

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
  lp_options_apply(argv[2]);
  for (int i = 0; lp_settings_keys[i]; i++) {
    const char *v = getenv(lp_settings_keys[i]);
    if (v && *v) printf("%s=%s\n", lp_settings_keys[i], v);
  }
  for (int i = 0; lp_options_keys[i]; i++) {
    const char *v = getenv(lp_options_keys[i]);
    if (v && *v) printf("%s=%s\n", lp_options_keys[i], v);
  }
  lp_settings_log(argv[1], stdout);
  lp_options_log(argv[2], stdout);
  const char *g = getenv("RS_NETGUARD");
  printf("RS_NETGUARD=%s\n", g ? g : "unset");
  return 0;
}
"""
BASH = r"""
set -u
eval "$SNIPPET"
settings_env "$1"
options_env "$2"
for k in "${settings_keys[@]}"; do [[ -n ${!k:-} ]] && echo "$k=${!k}"; done
for k in "${options_keys[@]}"; do [[ -n ${!k:-} ]] && echo "$k=${!k}"; done
[[ ${#settings_log[@]} == 0 ]] || printf '%s\n' "${settings_log[@]}"
[[ ${#options_log[@]} == 0 ]] || printf '%s\n' "${options_log[@]}"
echo "RS_NETGUARD=${RS_NETGUARD-unset}"
"""
# The Mac launcher's --env pairs: what open gives the game besides launchd's environment, one word a line.
MAC_ARGS = r"""
set -u
eval "$SNIPPET"
settings_env "$1"
options_env "$2"
[[ ${#settings_args[@]} == 0 ]] || printf '%s\n' "${settings_args[@]}"
[[ ${#options_args[@]} == 0 ]] || printf '%s\n' "${options_args[@]}"
"""


def read(path):
    with open(path) as f:
        return f.read()


def launch_snippet():
    """launch.sh's settings_keys, options_keys, options_toggles, settings_env and options_env, as written there."""
    src = read(LAUNCH)
    keys = re.search(r"^settings_keys=\(.*\)$", src, re.M)
    okeys = re.search(r"^options_keys=\((.*?)\)$", src, re.M | re.S)
    toggles = re.search(r'^options_toggles=".*"$', src, re.M)
    fn = re.search(r"^settings_env\(\) \{.*?^\}$", src, re.M | re.S)
    ofn = re.search(r"^options_env\(\) \{.*?^\}$", src, re.M | re.S)
    assert keys and okeys and toggles and fn and ofn, "launch.sh has no settings/options readers"
    return "\n".join([keys.group(0), okeys.group(0), toggles.group(0), fn.group(0), ofn.group(0)]) + "\n"


def launch_keys():
    return re.search(r"^settings_keys=\((.*)\)$", read(LAUNCH), re.M).group(1).split()


def launch_options():
    return re.search(r"^options_keys=\((.*?)\)$", read(LAUNCH), re.M | re.S).group(1).split()


def lp_keys():
    src = read(os.path.join(LP_DIR, "lp_settings.c"))
    body = re.search(r"lp_settings_keys\[\] = \{(.*?)\};", src, re.S).group(1)
    return re.findall(r'"([A-Z0-9_]+)"', body)


def lp_options():
    src = read(os.path.join(LP_DIR, "lp_settings.c"))
    body = re.search(r"lp_options_keys\[\] = \{(.*?)\};", src, re.S).group(1)
    return re.findall(r'"([A-Z0-9_]+)"', body)


def installer_keys():
    return re.findall(r'BuiltInMod\(key: "([A-Z0-9_]+)"', read(INSTALLER))


def installer_options():
    return re.findall(r'OptionSpec\(key: "([A-Z0-9_]+)"', read(INSTALLER))


class Keys(unittest.TestCase):
    def test_same_list_everywhere(self):
        self.assertTrue(launch_keys())
        self.assertEqual(launch_keys(), lp_keys())
        self.assertEqual(launch_keys(), installer_keys())

    def test_options_same_list_everywhere(self):
        self.assertTrue(launch_options())
        self.assertEqual(launch_options(), lp_options())
        self.assertEqual(launch_options(), installer_options())

    def test_nothing_that_weakens_offline(self):
        # The network guard and the Steam layer's switches stay out of a file anyone can write.
        for k in launch_keys():
            self.assertTrue(k.startswith("RS_MODS_") or k == "RS_REARMED", k)
            self.assertNotIn(k, ("RS_MODS_PATH", "RS_MODS_LOG"))

    def test_options_nothing_that_weakens_offline(self):
        # options.env may name the Steam layer's song and account switches, but never the offline
        # layer itself, its folder or library, the kept credentials, or an arbitrary mods path.
        for k in launch_options():
            self.assertTrue(k.startswith("RS_"), k)
            self.assertNotIn(k, ("RS_NETGUARD", "RS_NETGUARD_LOG", "RS_NETGUARD_BT",
                                 "RS_STEAM_KEEP_CREDENTIALS", "RS_STEAM_MODE",
                                 "RS_STEAM_DATA_DIR", "RS_STEAM_REAL_LIB",
                                 "RS_MODS_PATH", "RS_MODS_NO_THREAD"))
        self.assertEqual(set(launch_keys()) & set(launch_options()), set())


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

    def write(self, content):
        """A made-up settings.env or options.env with CONTENT (None: no file); its path."""
        fd, path = tempfile.mkstemp(prefix="rs-settings-", dir=self.tmp)
        os.close(fd)
        os.unlink(path)
        if content is not None:
            with open(path, "w", newline="") as f:
                f.write(content)
        return path

    def env(self, env=None):
        return dict({"PATH": "/usr/bin:/bin", "HOME": self.tmp}, **(env or {}))

    def run_both(self, settings_content, options_content=None, env=None):
        """Both readers on files with the contents (None: no file), with ENV; their outputs, which must agree."""
        spath = self.write(settings_content)
        opath = self.write(options_content)
        base = self.env(env)
        mac = subprocess.run(["/bin/bash", "-c", BASH, "test", spath, opath], capture_output=True, text=True,
                             env=dict(base, SNIPPET=launch_snippet()))
        self.assertEqual(mac.returncode, 0, mac.stderr)
        self.assertEqual(mac.stderr, "")
        if self.cc_error:
            self.fail("lp_settings.c did not compile: " + self.cc_error)
        lp = subprocess.run([self.lp, spath, opath], capture_output=True, text=True, env=base)
        self.assertEqual(lp.returncode, 0, lp.stderr)
        self.assertEqual(mac.stdout, lp.stdout, "the Mac and Learn & Play readers disagree")
        return mac.stdout.splitlines()

    def test_no_file(self):
        self.assertEqual(self.run_both(None), ["RS_NETGUARD=unset"])

    def test_installer_file(self):
        out = self.run_both("# Written by the installer (Built-in Mods…)\n# more words\n\nRS_MODS_INTRO=0\nRS_REARMED=0\n")
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
        out = self.run_both("RS_MODS_INTRO=0\nRS_MODS_OVERLAY=0\n", None,
                            {"RS_MODS_INTRO": "1", "RS_MODS_OVERLAY": "0"})
        self.assertEqual(out, ["RS_MODS_INTRO=1", "RS_MODS_OVERLAY=0",
                               "[settings] RS_MODS_INTRO=0 not used: the environment has RS_MODS_INTRO=1",
                               "[settings] RS_MODS_OVERLAY=0", "RS_NETGUARD=unset"])

    def test_empty_environment_value_is_unset(self):
        out = self.run_both("RS_MODS_MULTIPLAYER=0\n", None, {"RS_MODS_MULTIPLAYER": ""})
        self.assertEqual(out, ["RS_MODS_MULTIPLAYER=0", "[settings] RS_MODS_MULTIPLAYER=0", "RS_NETGUARD=unset"])

    def test_first_of_a_repeated_key_counts(self):
        out = self.run_both("RS_MODS_INTRO=0\nRS_MODS_INTRO=1\n")
        self.assertEqual(out, ["RS_MODS_INTRO=0", "[settings] RS_MODS_INTRO=0",
                               "[settings] ignored: RS_MODS_INTRO=1 (repeated)", "RS_NETGUARD=unset"])

    def test_on_is_allowed(self):
        self.assertEqual(self.run_both("RS_MODS_BYPASS=1\n"),
                         ["RS_MODS_BYPASS=1", "[settings] RS_MODS_BYPASS=1", "RS_NETGUARD=unset"])

    def test_options_values(self):
        out = self.run_both(None, "RS_METAL_MSAA=2\nRS_AUDIO_FRAMES=128\nRS_STEAM_DLC=all\n")
        self.assertEqual(out, ["RS_AUDIO_FRAMES=128", "RS_METAL_MSAA=2", "RS_STEAM_DLC=all",
                               "[options] RS_METAL_MSAA=2", "[options] RS_AUDIO_FRAMES=128",
                               "[options] RS_STEAM_DLC=all", "RS_NETGUARD=unset"])

    def test_options_comments_and_blank_lines_silent(self):
        out = self.run_both(None, "# the sheet's file\n\nRS_AUDIO_FRAMES=128\n\n# another\nRS_METAL_MSAA=2\n\n")
        self.assertEqual(out, ["RS_AUDIO_FRAMES=128", "RS_METAL_MSAA=2",
                               "[options] RS_AUDIO_FRAMES=128", "[options] RS_METAL_MSAA=2",
                               "RS_NETGUARD=unset"])

    def test_options_ends_trimmed_values_keep_spaces(self):
        out = self.run_both(None, "  RS_STEAM_PERSONA = Player 1 \r\nRS_METAL_COLORS= vivid\n")
        self.assertEqual(out, ["RS_METAL_COLORS=vivid", "RS_STEAM_PERSONA=Player 1",
                               "[options] RS_STEAM_PERSONA=Player 1", "[options] RS_METAL_COLORS=vivid",
                               "RS_NETGUARD=unset"])

    def test_options_other_lines_ignored(self):
        out = self.run_both(None, "RS_NETGUARD=log\nRS_STEAM_MODE=stub\nRS_MODS_PATH=/tmp\n"
                                  "RS_AUDIO_RT=2\nRS_METAL_MSAA\nhello world\nRS_AUDIO_RT=0\n")
        self.assertEqual(out, ["RS_AUDIO_RT=0",
                               "[options] ignored: RS_NETGUARD=log",
                               "[options] ignored: RS_STEAM_MODE=stub",
                               "[options] ignored: RS_MODS_PATH=/tmp",
                               "[options] ignored: RS_AUDIO_RT=2",
                               "[options] ignored: RS_METAL_MSAA",
                               "[options] ignored: hello world",
                               "[options] RS_AUDIO_RT=0",
                               "RS_NETGUARD=unset"])

    def test_options_environment_wins(self):
        out = self.run_both(None, "RS_AUDIO_FRAMES=128\nRS_METAL_MSAA=2\n", {"RS_AUDIO_FRAMES": "512"})
        self.assertEqual(out, ["RS_AUDIO_FRAMES=512", "RS_METAL_MSAA=2",
                               "[options] RS_AUDIO_FRAMES=128 not used: the environment has RS_AUDIO_FRAMES=512",
                               "[options] RS_METAL_MSAA=2", "RS_NETGUARD=unset"])

    def test_options_first_of_a_repeated_key_counts(self):
        out = self.run_both(None, "RS_AUDIO_FRAMES=128\nRS_AUDIO_FRAMES=256\n")
        self.assertEqual(out, ["RS_AUDIO_FRAMES=128", "[options] RS_AUDIO_FRAMES=128",
                               "[options] ignored: RS_AUDIO_FRAMES=256 (repeated)", "RS_NETGUARD=unset"])

    def test_mac_game_gets_what_is_logged(self):
        # open starts the game with launchd's environment and the --env pairs, not the launcher's environment: a
        # value the launcher was given another way (open --env, a Terminal) reaches the game only as a pair, so
        # the value the log says is used, the file's or the environment's, is always one. Learn & Play's reader
        # sets its own process's environment, which the game is.
        r = subprocess.run(["/bin/bash", "-c", MAC_ARGS, "test",
                            self.write("RS_MODS_INTRO=0\nRS_MODS_OVERLAY=0\nRS_MODS_FIXES=0\nRS_NETGUARD=log\n"),
                            self.write("RS_AUDIO_FRAMES=128\nRS_METAL_MSAA=2\nRS_STEAM_MODE=stub\n")],
                           capture_output=True, text=True,
                           env=dict(self.env({"RS_MODS_INTRO": "1", "RS_MODS_OVERLAY": "0"}), SNIPPET=launch_snippet()))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.splitlines(), ["--env", "RS_MODS_INTRO=1", "--env", "RS_MODS_OVERLAY=0",
                                                 "--env", "RS_MODS_FIXES=0",
                                                 "--env", "RS_AUDIO_FRAMES=128", "--env", "RS_METAL_MSAA=2"])


if __name__ == "__main__":
    unittest.main()
