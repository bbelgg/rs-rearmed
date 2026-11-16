// settings.env in LP_HOME (docs/windows/INSTALL.md "Settings and switches"): the built-in mods switched off
// in the installer (Settings and switches...), read as the Mac launcher reads its own (tools/play-native/launch.sh).
// Lines KEY=0 or KEY=1 of lp_settings_keys, whitespace ignored, # a comment; any other line is ignored, and
// of a key given twice the first counts. Each is set unless the environment has another value (open --env,
// launchctl setenv win). tools/play-native/test_settings.py runs both readers on the same files and checks
// that their lists of keys and the installer's agree.
//
// options.env beside it holds every other switch the installer's sheet offers (lp_options_keys): toggles as
// KEY=0 or KEY=1, every other key as KEY=value with the ends' whitespace ignored. Same first-counts and
// environment-wins rules, "[options] ..." log lines, and the same three-way agreement with the Mac launcher
// and the installer.
#pragma once
#include <stdio.h>

extern const char *const lp_settings_keys[];  // NULL-terminated
extern const char *const lp_options_keys[];   // NULL-terminated

// Sets the file's switches in the environment, those the environment doesn't have (a missing file: none).
void lp_settings_apply(const char *path);

// What the file's lines mean now, one "[settings] ..." line each, in the Mac launcher's words: KEY=V, KEY=V not
// used (the environment has another value), or ignored. After lp_settings_apply, also in a process started
// again (mods_guard), which reads the same file and finds its own switches set already.
void lp_settings_log(const char *path, FILE *out);

// options.env: the same, with "[options] ..." lines.
void lp_options_apply(const char *path);
void lp_options_log(const char *path, FILE *out);
