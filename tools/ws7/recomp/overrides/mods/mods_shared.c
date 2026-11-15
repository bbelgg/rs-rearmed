// The built-in fixes' part that is one source for both editions (mods_shared.h): the switches, the log and the start.
// A fix that needs nothing of one game alone is here too, written as a mod is (rc_mod.h): it finds the game's
// functions and fields by name when it runs, so the same object is right for either game. Built into both apps
// (overrides/mods/build.sh, win-x86/build/build_shims.sh).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_mod.h"
#include "rc_nofile.h"

#include "framerate.h"
#include "mods_shared.h"

// ------------------------------------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------------------------------------
rs_mods_config rs_mods_cfg = {
    .bypass = 1, .fixes = 1, .autosave = 1, .multiplayer = 1, .budget_s = 0.050, .intro = 1, .overlay = 1};

int rs_mods_env_on(const char *name, int dflt) {
  const char *v = getenv(name);
  if (!v || !*v) return dflt;
  return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

void rs_mods_reload_config(void) {
  rs_mods_cfg.log = rs_mods_env_on("RS_MODS_LOG", 0);
  rs_mods_cfg.bypass = rs_mods_env_on("RS_MODS_BYPASS", 1);
  rs_mods_cfg.fixes = rs_mods_env_on("RS_MODS_FIXES", 1);
  rs_mods_cfg.intro = rs_mods_env_on("RS_MODS_INTRO", 1);
  rs_mods_cfg.overlay = rs_mods_env_on("RS_MODS_OVERLAY", 1);
  rs_mods_cfg.autosave = rs_mods_env_on("RS_MODS_AUTOSAVE", 1);
  rs_mods_cfg.multiplayer = rs_mods_env_on("RS_MODS_MULTIPLAYER", 1);
  rs_mods_cfg.budget_s = 0.050;
  const char *b = getenv("RS_MODS_BUDGET_MS");
  if (b && *b) {
    char *end;
    double ms = strtod(b, &end);
    if (end != b && ms >= 0 && ms <= 1000) rs_mods_cfg.budget_s = ms / 1000;
  }
}
const rs_mods_config *rs_mods_config_get(void) { return &rs_mods_cfg; }

// The line is written whole: the game's threads and the mods log to the same stream, and a line of theirs between
// the prefix and the text would hide both from whoever reads the log by its line starts.
void rs_mods_logf(const char *fmt, ...) {
  if (!rs_mods_cfg.log) return;
  va_list ap;
  va_start(ap, fmt);
  flockfile(stderr);
  fputs("[rs-mods] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  funlockfile(stderr);
  va_end(ap);
}

// ------------------------------------------------------------------------------------------------
// OS-level fix: open-file limit (rsmacfixes fix 4), which the batched DLC mounting needs: a large library has
// more archives open than the 256 descriptors of an app started from Finder.
// ------------------------------------------------------------------------------------------------
uint64_t rs_mods_raise_nofile(void) {
  uint64_t old;
  int failed;
  uint64_t now = rc_raise_nofile(&old, &failed);
  if (now != old || failed)
    rs_mods_logf("open-file limit %llu -> %llu%s", (unsigned long long)old, (unsigned long long)now,
                 failed ? " (setrlimit failed)" : "");
  return now;
}

// ------------------------------------------------------------------------------------------------
// No intro screens. RSBootService (RSBootService.cpp) boots in phases (u32 at boot_phase): 0 and 1 start the
// services and the preload groups; 2 ("OnInit_Intro") waits for the shader, database, GE, UI, reactive-effects
// and fork services; 3 runs boot_phase3_steps steps (u32 counter at boot_step: the patch and other archives
// mounted, the venues) and then loads the start-up sound bank and, once the intro is over, pushes the title
// screen. Phases 2 and 3 first tick the intro (boot_intro_update: the Scaleform intro movie with the logos,
// legal screens and intro video) and wait while it returns true. Its state (u32 at boot_intro_state): 0 load
// the movie's model, 1 wait for it, 2 wait for the movie to start (busy), 3 play (not busy: the boot goes on
// meanwhile), 4 end (release the model, state 5, start the boot service's 3-second tail timer), 5 done.
//
// The game's own skip (in phase 2, just before the tick: a key press once the movie is on its last screen) sets
// state 4 and clears the playing flag (boot_intro_playing). The skip here takes the same transition, without
// the movie ever playing: the model is loaded as usual (states 0 and 1); then, instead of starting the movie,
// the tick reports "not busy" (as it does while the movie plays) until phase 3 has done its steps, and then
// ends the intro. Both conditions are needed, measured on the Mac build: without the model loaded, or with
// the intro ended before phase 3's steps, the title screen never gets its menu movie and the game sits on an
// empty screen (the stall that RSMods' short "fast load" intros also run into, see its QualityOfLife.cpp).
// The screen is white, then black for a few seconds while the title loads. Only during boot (phases 2 and 3).
// With RS_MODS_INTRO=0 the intro plays.
//
// Both games have the tick as a function of its own with the same states and the same fields (each one's
// offsets.map says where they were read), so this is a pre hook on it for both: Learn & Play's is a stdcall
// function, and the hook sees the Mac's call (rc_hook.h).
// ------------------------------------------------------------------------------------------------
static int before_intro_update(rc_ctx *c, void *user) {
  (void)user;
  if (!rs_mods_cfg.intro) return 0;
  OV_ARGS(a, c);
  uint8_t *boot = OV_PTR(a, uint8_t);
  uint32_t phase = RC_LD(uint32_t, boot + RC_OFF(boot_phase));
  uint32_t state = RC_LD(uint32_t, boot + RC_OFF(boot_intro_state));
  if ((phase != 2 && phase != 3) || state < 2 || state >= 4) return 0;  // loading the model: the original
  if (phase == 2 || RC_LD(uint32_t, boot + RC_OFF(boot_step)) < (uint32_t)RC_OFF(boot_phase3_steps)) {
    ov_ret_u32(c, 0);  // not busy, the movie not started
    return 1;
  }
  rs_mods_logf("intro: skipped");
  RC_ST(uint32_t, boot + RC_OFF(boot_intro_state), 4);  // the game's skip: the original ends the intro (state 5)
  boot[RC_OFF(boot_intro_playing)] = 0;                 // and starts the tail timer
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Song ownership during a mount. Mounting an archive asks whether its AppID is in the DLC list the game made
// once at start (steam_dlc_owned), so no answer of the Steam layer can cover a song added while the game runs
// (shims/steam/src/enumerate.h). During the install step of the DLC coordinator's update an archive in dlc/
// counts as owned, whatever AppID it carries; every other caller gets the game's answer, and RS_MODS_BYPASS=0
// asks the list during a mount too. The update is each edition's own (their batched mounting loops differ:
// mods.c, win-x86/shims/win32_overrides.c), and says here when it is in its steps.
// ------------------------------------------------------------------------------------------------
static int g_mounting;  // the game thread's

void rs_mods_mounting(int on) { g_mounting = on; }

static int before_dlc_owned(rc_ctx *c, void *user) {
  (void)user;
  if (!g_mounting || !rs_mods_cfg.bypass) return 0;
  ov_ret_u32(c, 1);
  return 1;
}

// ------------------------------------------------------------------------------------------------
// Two players on any input: Player 2's saved input. A player's input mode and input live in their profile, but
// the game restores a saved input for Player 1 alone, after the profile screen; Player 2 takes the first slot
// free. So before the game looks for a slot for Player 2's guitar (input_bind), this finds the input their profile
// names, when the profile is on Direct Connect or the microphone, and gives it the slot. It has to be there that
// early: moving the input of a running stream crashes the I/O thread, in the callback that feeds the player.
// RS_MODS_MULTIPLAYER=0 is the original. The fix's other half is each edition's own (mods_shared.h).
//
// Both games have the function: the Mac's takes (input, force), Learn & Play's the input in a register, with
// `force` folded away, 0 in every call of either game (each one's symbols.map has the reading), and the hook sees
// the Mac's call. What it reads is the same in both: the ten slots (input_binding: 0-3 an input, 5 nothing), the
// profile's keys (pointers to their text), a string of the profile, the input of a name and its slot.
// Each edition's own version of this left the profile's text to the game when it was too long to be in the
// string, a block of the game's memory at every call; this one frees it.
// ------------------------------------------------------------------------------------------------
#define INPUT_P1_MIC 1
#define INPUT_P2_GUITAR 2
#define INPUT_P2_MIC 3
#define INPUT_NONE 5
#define INPUT_SLOTS 10
#define P2_STRING 64  // room for the game's small string in either game
#define P2_NAME 256

// A pointer for the game, at its own width
static void put_ptr(uint8_t *at, const void *p) {
  uint64_t g = RC_GUEST(p);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}

// The string of Player 2's profile under `key`, as text in `name` (P2_NAME bytes of the game's memory, as is `s`):
// empty when the profile has none. The game writes it into a small string of its own: the text in place or behind
// a pointer at its start, its end at efd_str_size, and efd_str_inline holds the address of that word while the
// text is in place. A text behind the pointer is the game's allocation, for a name too long to be in place: once
// the name is copied the string is destroyed as the game's own callers of input_profile_string destroy theirs
// (efd_str_destroy, a call in Learn & Play at 0x11dee5e, the same steps inlined on the Mac at 0x1003594b9), which
// frees that text and nothing when it is in place.
static void profile_string(const void *key, uint8_t *s, char *name) {
  uint8_t *end = s + RC_OFF(efd_str_size), *in_place = s + RC_OFF(efd_str_inline);
  memset(s, 0, P2_STRING);
  put_ptr(end, s);
  put_ptr(in_place, end);
  OV_GUEST(input_profile_string, RS_MODS_PLAYER_2, (uint64_t)(uintptr_t)key, (uint64_t)(uintptr_t)s);
  const char *from = rc_mod_ptr(in_place) == (void *)end ? (const char *)s : rc_mod_ptr(s);
  const char *to = rc_mod_ptr(end);
  size_t len = from && to > from ? (size_t)(to - from) : 0;
  if (len >= P2_NAME) len = P2_NAME - 1;
  if (len) memcpy(name, from, len);
  name[len] = 0;
  OV_GUEST(efd_str_destroy, (uint64_t)(uintptr_t)s);
}

static int before_input_bind(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  if (!rs_mods_cfg.multiplayer || OV_U32(a) != INPUT_P2_GUITAR) return 0;
  uint8_t mode = (uint8_t)OV_GUEST(input_mode, RS_MODS_PLAYER_2);
  if (!rs_mods_any_input(mode)) return 0;
  int32_t *binding = (int32_t *)(uintptr_t)RC_GA(input_binding);
  for (int i = 0; i < INPUT_SLOTS; i++)
    if (binding[i] == INPUT_P2_GUITAR) return 0;
  // what the game is handed has to be memory it can reach (rc_mod.h): one block a thread, kept
  static _Thread_local uint8_t *mem;
  if (!mem && !(mem = rc_guest_alloc(P2_STRING + P2_NAME))) return 0;
  char *name = (char *)mem + P2_STRING;
  const void *key = rc_mod_ptr((const void *)(uintptr_t)(mode == RS_MODS_MODE_DIRECT ? RC_GA(input_key_direct)
                                                                                   : RC_GA(input_key_mic)));
  if (!key) return 0;
  profile_string(key, mem, name);
  if (!name[0]) return 0;
  uint64_t manager = OV_GUESTV(input_manager, NULL, 0);
  const uint8_t *in = RC_HOST(OV_GUEST(input_by_name, manager, (uint64_t)(uintptr_t)name));
  if (!in) return 0;
  uint32_t slot = RC_LD(uint32_t, in + RC_OFF(input_slot));
  if (slot >= INPUT_SLOTS) return 0;
  int32_t held = binding[slot];  // a microphone input takes any slot, and finds another
  if (held == INPUT_NONE || held == INPUT_P1_MIC || held == INPUT_P2_MIC) {
    binding[slot] = INPUT_P2_GUITAR;
    rs_mods_logf("player 2 on \"%s\"", name);
  }
  return 0;
}

void rs_mods_start(void) {
  rs_mods_reload_config();
  if (rc_hook_add_name("boot_intro_update", before_intro_update, NULL, NULL)) rs_mods_logf("no intro hook point");
  if (rc_hook_add_name("steam_dlc_owned", before_dlc_owned, NULL, NULL)) rs_mods_logf("no DLC ownership hook point");
  if (rc_hook_add_name("input_bind", before_input_bind, NULL, NULL))
    rs_mods_logf("no hook point for Player 2's saved input");
  if (rs_mods_cfg.fixes) rs_mods_raise_nofile();
  rs_mods_logf("fixes %s, bypass %s, overlay %s, intro skip %s, autosave %s, budget %.0f ms",
               rs_mods_cfg.fixes ? "on" : "off", rs_mods_cfg.bypass ? "on" : "off", rs_mods_cfg.overlay ? "on" : "off",
               rs_mods_cfg.intro ? "on" : "off", rs_mods_cfg.autosave ? "on" : "off", rs_mods_cfg.budget_s * 1000);
  int fr = rs_framerate_init();  // framerate.c: its own switch, RS_MODS_FRAME_RATE
  if (fr < 0) rs_mods_logf("frame rate: off");
  else rs_mods_logf("frame rate: on, %d of %d points", fr, RS_FR_POINTS);
}
