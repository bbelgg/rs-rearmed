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
//   RS_MODS_OVERLAY=0  the cache overlay off: the cacheN.7z archives come from cache.psarc only, without the
//                      menu entries the installer's overlay adds (Direct Connect) (default on)
//   RS_MODS_FRAME_RATE=0 what the game counts in frames (calibration, the tuner) is left to the frame rate, as in
//                      the original (default: as at 60 Hz at any rate; framerate.h)
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
} rs_mods_config;
// The switches as they were last read; the defaults until then.
extern rs_mods_config rs_mods_cfg;
const rs_mods_config *rs_mods_config_get(void);
void rs_mods_reload_config(void);
// A switch: off when its value starts with 0, n, N, f or F, on with any other, dflt when unset or empty.
int rs_mods_env_on(const char *name, int dflt);

// A line of what a fix did, `[rs-mods] ...`, with RS_MODS_LOG=1; nothing without.
void rs_mods_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// The start of the built-in fixes, once the edition's environment is final and before the game runs: the Mac's
// from a constructor (the launcher has set it), Learn & Play's from lp_mods_init (after settings.env). Reads the
// switches, raises the open-file limit, adds the hooks of the fixes that are here and of the frame rate fixes
// (framerate.c), and logs what is on. The fixes that are here: no intro screens (RS_MODS_INTRO), an archive
// of dlc/ owned while it is mounted (RS_MODS_BYPASS), and Player 2's saved input (RS_MODS_MULTIPLAYER).
void rs_mods_start(void);
// The game thread's: the DLC coordinator's update is in its install steps (1) or out of them again (0). Each
// edition's dlc_update says so around the steps; an archive counts as owned between the two.
void rs_mods_mounting(int on);

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
