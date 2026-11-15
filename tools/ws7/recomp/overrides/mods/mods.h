// Overrides: the community mods (rsmacfixes, RSBypass) as native replacements of the translated
// functions they patched.
#ifndef RS_MODS_H
#define RS_MODS_H

#include <stdint.h>

#include "rc_ov.h"
#include "mods_shared.h"

// The switches (rs_mods_config, rs_mods_cfg), the log and the start, one source with Learn & Play: mods_shared.h. Here
// there is one switch more, for tests: RS_MODS_NO_THREAD=1 does not start the End-key poller thread.
// One pass of the End-key fix over the keyboard table (the poller thread runs it every 100 ms).
// Returns 1 if it added the entry.
int rs_mods_keys_poll_once(void);

typedef struct {
  unsigned long path_hits, path_misses, installed, frames, bypassed;
} rs_mods_stats;
rs_mods_stats rs_mods_stats_get(void);
void rs_mods_stats_reset(void);

// The overrides (guest function entry points).
void RC_FN(num_dtor)(rc_ctx *c);  // JSON number object destructor (removes its node)
void RC_FN(num_intern)(rc_ctx *c);  // JSON number interning (insert/find)
void RC_FN(num_rehash)(rc_ctx *c);  // JSON number table rehash
void RC_FN(path_md5)(rc_ctx *c);  // path -> interned path object (MD5 cache)
void RC_FN(dlc_update)(rc_ctx *c);  // DLC coordinator per-frame update (batched mounting)
void RC_FN(key_down)(rc_ctx *c);  // game keyboard: key down
void RC_FN(key_up)(rc_ctx *c);  // game keyboard: key up
void RC_FN(thread_set_priority)(rc_ctx *c);  // thread class SetPriority
void RC_FN(sng_verify)(rc_ctx *c);  // DSA verify (the SNG caller is bypassed)

// Addresses of the overrides, like the generated modules' tables (tests register them in a fake image).
extern const ov_entry ov_mods_entries[];
extern const int OV_MODS_COUNT;

#endif
