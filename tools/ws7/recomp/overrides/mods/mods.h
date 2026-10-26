// Overrides: the community mods (rsmacfixes, RSBypass) as native replacements of the translated
// functions they patched.
#ifndef RS_MODS_H
#define RS_MODS_H

#include <stdint.h>

#include "rc_ov.h"

// Run-time switches (environment, read at startup and by rs_mods_reload_config):
//   RS_MODS_BYPASS=0   RSBypass off: the .sng signature check is enforced (default on)
//   RS_MODS_FIXES=0    rsmacfixes off: keyboard keys, path-MD5 cache, batched DLC mounting, number-table
//                      hash, thread priority raised, open-file limit; every overridden function then
//                      behaves exactly like the original (default on)
//   RS_MODS_BUDGET_MS  per-frame budget of the batched DLC mounting (default 50; 0 = one per frame)
//   RS_MODS_NO_THREAD=1  do not start the End-key poller thread (tests)
//   RS_MODS_LOG=1      log what is applied to stderr
typedef struct {
  int bypass;
  int fixes;
  double budget_s;
} rs_mods_config;

const rs_mods_config *rs_mods_config_get(void);
void rs_mods_reload_config(void);
// Raises RLIMIT_NOFILE (rsmacfixes' fix 4); returns the new soft limit.
uint64_t rs_mods_raise_nofile(void);
// One pass of the End-key fix over the keyboard table (the poller thread runs it every 100 ms).
// Returns 1 if it added the entry.
int rs_mods_keys_poll_once(void);

typedef struct {
  unsigned long path_hits, path_misses, installed, frames, bypassed;
} rs_mods_stats;
rs_mods_stats rs_mods_stats_get(void);
void rs_mods_stats_reset(void);

// The overrides (guest function entry points).
void F_1000f8d20(rc_ctx *c);  // JSON number object destructor (removes its node)
void F_1000f8f10(rc_ctx *c);  // JSON number interning (insert/find)
void F_1000f9920(rc_ctx *c);  // JSON number table rehash
void F_100158b70(rc_ctx *c);  // path -> interned path object (MD5 cache)
void F_100a0d730(rc_ctx *c);  // DLC coordinator per-frame update (batched mounting)
void F_101135520(rc_ctx *c);  // game keyboard: key down
void F_101135560(rc_ctx *c);  // game keyboard: key up
void F_101313170(rc_ctx *c);  // thread class SetPriority
void F_10137683d(rc_ctx *c);  // DSA verify (the SNG caller is bypassed)

// Addresses of the overrides, like the generated modules' tables (tests register them in a fake image).
extern const ov_entry ov_mods_entries[];
extern const int OV_MODS_COUNT;

#endif
