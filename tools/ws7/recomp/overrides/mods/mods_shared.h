// The built-in fixes: what is one source for both editions (mods_shared.c). Each edition has its own fixes beside it,
// on its game's own functions: mods.c for the Mac, win-x86/shims/win32_overrides.c for Learn & Play. They take
// the switches, the log and the start from here, so a switch, a default and a log line are the same in both.
#ifndef RS_MODS_SHARED_H
#define RS_MODS_SHARED_H

#include <stdint.h>

// Run-time switches (environment, read by rs_mods_start and rs_mods_reload_config):
//   RS_MODS_BYPASS=0   RSBypass off: the .sng signature check is enforced, and an archive's AppID is asked of the
//                      game's DLC list during a mount too (default on)
//   RS_MODS_FIXES=0    rsmacfixes off: the path-MD5 cache, batched DLC mounting, the number-table hash, the
//                      open-file limit, and the Mac's keyboard keys and thread priority; every overridden
//                      function then behaves exactly like the original (default on)
//   RS_MODS_INTRO=0    the intro screens (logos, legal screens, intro video) play at start-up (default:
//                      skipped, the game goes straight to the title screen)
//   RS_MODS_BUDGET_MS  per-frame budget of the batched DLC mounting (default 50; 0 = one per frame)
//   RS_MODS_AUTOSAVE=0 the Auto Save warning dialog is shown (default: skipped)
//   RS_MODS_MULTIPLAYER=0 Player 2 takes a Real Tone cable only, as in the original (default: any input)
//   RS_MODS_OVERLAY=0  the cache overlay off: the cacheN.7z archives come from cache.psarc only and the menu movies
//                      from static.psarc, without what the installer's overlay changes in them (Direct Connect, the
//                      lists' scroll bars) (default on)
//   RS_MODS_FRAME_RATE=0 what the game counts in frames (calibration, the tuner) is left to the frame rate, as in
//                      the original (default: as at 60 Hz at any rate; framerate.h)
//   RS_MODS_GUITARCADE_SKIP=0 a Guitarcade game's tuner has no Skip Tuner, as in the original (default: it has
//                      the one a song's tuner has)
//   RS_MODS_LOG=1      log what is applied, to stderr (the edition's log)
typedef struct {
  int bypass;
  int fixes;
  int autosave;
  int multiplayer;
  double budget_s;
  int intro;  // skip the intro screens
  int overlay;
  int log;
  int guitarcade_skip;  // Skip Tuner in a Guitarcade game's tuner
} rs_mods_config;
// The switches as they were last read; the defaults until then.
extern rs_mods_config rs_mods_cfg;
const rs_mods_config *rs_mods_config_get(void);
void rs_mods_reload_config(void);
// A switch: off when its value starts with 0, n, N, f or F, on with any other, dflt when unset or empty.
int rs_mods_env_on(const char *name, int dflt);

// A line of what a fix did, `[rs-mods] ...`, with RS_MODS_LOG=1; nothing without.
void rs_mods_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// The cache overlay (tools/play-native/cache_overlay.py: <game>/Update/patcharchives.psarc, which the engine mounts at
// PatchUpdate/): the name a file the game opens through FrontEnd_OpenFile has there, into alt (n bytes), for each
// edition's hook to open first. A cacheN.7z of cache.psarc is patchupdate/<path> as it is; a menu movie of
// static.psarc, ...gfxassets\views\<name>.gfx with '\' or '/' in any case (the game opens them as
// data\gfxassets\views\<name>.gfx), is patchupdate/gfxassets/views/<name>.gfx in lower case. 0 for any other file
// (the game opens it as it is) and for a patchupdate/ path, which is the overlay's already.
int rs_mods_overlay_name(const char *path, char *alt, size_t n);

// The start of the built-in fixes, once the edition's environment is final and before the game runs: the Mac's
// from a constructor (the launcher has set it), Learn & Play's from lp_mods_init (after settings.env). Reads the
// switches, raises the open-file limit, adds the hooks of the fixes that are here and of the frame rate fixes
// (framerate.c), and logs what is on. The fixes that are here: no intro screens (RS_MODS_INTRO), an archive
// of dlc/ owned while it is mounted (RS_MODS_BYPASS), the batched mounting's song manifests and file questions
// (RS_MODS_FIXES), Player 2's saved input (RS_MODS_MULTIPLAYER), and Skip Tuner in a Guitarcade game's tuner
// (RS_MODS_GUITARCADE_SKIP).
void rs_mods_start(void);
// Batched DLC mounting (RS_MODS_FIXES, RS_MODS_BUDGET_MS): what is one source of it (mods_shared.c says what it is
// for). Each edition's dlc_update runs the install steps and waits for a mount in its game's own way; all of these
// are the game thread's, called from there:
//   rs_mods_mount_frame(installing)  first in every update, with the coordinator's installing flag. 1: this update's
//                                    steps run in the loop with the budget. A mounting's first frame starts its
//                                    counts, and the loader thread's pause follows the answer.
//   rs_mods_mounting(1), (0)         around each install step: an archive counts as owned between the two
//                                    (RS_MODS_BYPASS), and the step's file questions are the ones answered here.
//   rs_mods_mount_took()             after a step that took an archive (the flag is still set).
//   rs_mods_mount_loading(left)      1 while the steps have to wait for what the last one left to load. `left` is the
//                                    edition's number for it: 0 when nothing is left, otherwise one that changes
//                                    whenever something is done. No change for 5 s ends the waiting for this
//                                    mounting, with a line in the log.
//   rs_mods_mount_listed()           when a step of the loop has found the list empty and cleared the flag: the file
//                                    questions' check. 1: song manifests asked for are not in yet; the update sets
//                                    the flag again, and from the next one on
//   rs_mods_mount_holding()          is 1: the update takes no step, and clears the flag once
//   rs_mods_mount_waiting()          is 0: the manifests are in, or none has come for 5 s.
//   rs_mods_mount_end()              last, in the update that found the flag set and leaves it clear: the log's
//                                    lines, and the mounting's state is gone.
//   rs_mods_mount_restarted()        when the game started an enumeration in this update (its check said so): one
//                                    that comes while the flag is held ends the hold, and its archives are stepped
//                                    from the next update on, as a mounting of their own.
// rs_mods_mount_loading is the Mac loop's; Learn & Play's waits on the archives its Win32 seam counts open.
int rs_mods_mount_frame(int installing);
void rs_mods_mounting(int on);
void rs_mods_mount_took(void);
int rs_mods_mount_loading(uint64_t left);
int rs_mods_mount_listed(void);
int rs_mods_mount_holding(void);
int rs_mods_mount_waiting(void);
void rs_mods_mount_end(void);
void rs_mods_mount_restarted(void);
// The asset loader thread's pause between two passes, in microseconds: short while archives are mounted in the loop,
// the game's 10 ms at any other time. For each edition's replacement of the thread's loop (asset_loader_thread).
unsigned rs_mods_loader_pause_us(void);

// Two players on any input (RS_MODS_MULTIPLAYER), the game's numbers: players count from 0, and a player's input
// mode (input_mode) is 0 Real Tone cable, 1 microphone, 2 disconnected, 3 Direct Connect, 4 not set. The fix takes a
// profile on the microphone or Direct Connect as it is. Its first half, the Multiplayer screens' rule that Player 2
// is on a cable, is each edition's own (the games have it in different functions: mods.c, win32_overrides.c); the
// second, Player 2's saved input, is here.
#define RS_MODS_PLAYER_2 1
#define RS_MODS_MODE_MIC 1
#define RS_MODS_MODE_DIRECT 3
static inline int rs_mods_any_input(uint8_t mode) { return mode == RS_MODS_MODE_MIC || mode == RS_MODS_MODE_DIRECT; }

// Raises RLIMIT_NOFILE (rsmacfixes' fix 4, rc_nofile.h); returns the new soft limit.
uint64_t rs_mods_raise_nofile(void);

#endif
