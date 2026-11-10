// What a mod includes: one source, one arm64 dylib, for both editions. Build it with
//   clang -arch arm64 -dynamiclib -undefined dynamic_lookup -I<runtime> -I<overrides> mod.c -o mod.dylib
// and put it in the edition's mods folder. It is the Mac's interface (rc_hook.h, rc_ov.h) with the parts
// that hold for every target:
//
//   static int before(rc_ctx *c, void *user) {
//     OV_ARGS(a, c);                                  // the SysV call, whatever the game's convention
//     const char *boot = OV_PTR(a, const char);       // a host pointer
//     uint32_t phase = RC_LD(uint32_t, boot + RC_OFF(boot_phase));  // the field on this game
//     void *next = rc_mod_ptr(boot + RC_OFF(some_ptr));             // a pointer field, of this game's width
//     return 0;
//   }
//   __attribute__((constructor)) static void init(void) {
//     if (rc_hook_add_name("boot_intro_update", before, NULL, NULL)) return;  // not on this game: logged
//   }
//
// RC_OFF, RC_SLOT, RC_ADDR and RC_GA look the name up when first used; a name the game doesn't have logs a line and
// gives RC_NO_OFF (test RC_HAS_OFF) or 0. rc_target() says which game it is.
//
// Keep out of the source what only one game has: a literal address or offset, a guest pointer read with a
// C type (`*(void **)p`, a struct with pointers in it), c->r[...] other than through rc_args and ov_ret_*.
// A guest pointer read by rc_mod_ptr, or a value from the game read by RC_LD at its own width, is the same on
// both. A pointer given to the game must be its own memory (the view can't give it the mod's): the game gets
// null, with a log line: rc_guest_alloc gives memory the game can be given, on both. OV_GUEST(name, ...) calls a game
// function by its convention, rc_call_name_f one with a float among its arguments; what they return is the guest's
// value: a pointer goes through RC_HOST. rc_mod_set_arg changes an argument of the call a pre hook sees.
//
// rc_file_provide (rc_file.h) has the mod serve files to the game: ones it opens, sizes and finds in a folder.
//
// The offline rules hold for a mod as for the game: the network guard is loaded before any mod in both editions.
#ifndef RC_MOD_H
#define RC_MOD_H

#define RC_MOD 1
#include "rc_file.h"
#include "rc_hook.h"
#include "rc_mid.h"
#include "rc_ov.h"

// Marks this library as built for every target (rc_mods.c): Learn & Play loads no other
__attribute__((weak, used, visibility("default"))) const int rc_mod_abi = 1;

// The guest pointer at host address p, as a host pointer (null stays null)
static inline void *rc_mod_ptr(const void *p) {
  if (rc_guest_ptr_size == 8) return (void *)(uintptr_t)RC_LD(uint64_t, p);
  return RC_HOST(RC_LD(uint32_t, p));
}

// Integer or pointer argument k (from 0, up to 5) of the SysV call a pre hook sees (rc_hook.h)
static inline void rc_mod_set_arg(rc_ctx *c, int k, uint64_t v) {
  static const uint8_t order[6] = {7, 6, 2, 1, 8, 9};
  c->r[order[k]] = v;
}

#endif
