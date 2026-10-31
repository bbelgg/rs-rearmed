// Hook points: guest functions the build wraps (gen_image.py, skip-list lines `ADDR wrap`, named in
// tools/targets/names/hooks.list) so that native code loaded at run time (the mods, app/rc_main.c) can run before
// and after them. The executable exports these functions and the runtime (rc_call_guest, rc_slide, ...) to the mods.
//
// A mod finds a hook point by its name (rc_hook_add_name("game_frame", ...)): the same call works for every
// game the app can be built from (rc_target() says which one this is). rc_hook_add with an image address keeps
// working; a mac-x64 address finds the point of the same name on any target.
//
// A hook point's F_<addr> is rc_hook_call(index, c, W_<addr>), W_<addr> the translated function. The pre
// hooks see the entry state: the return address at c->r[4], the SysV arguments in the registers (read them
// with rc_args, see rc_ov.h's OV_ARGS); a pre hook that returns nonzero has done the call itself (set the
// result and popped the return address, as the ov_ret_* helpers of rc_ov.h do) and the original and the
// later hooks do not run. The post hooks run after the original, with its result in c->r[0] / c->x[0].
// Hooks may call translated code (rc_call_guest). Register them before the game starts (a library's
// constructor); the table is not locked.
#ifndef RC_HOOK_H
#define RC_HOOK_H

#include <stdint.h>

#include "rc.h"

typedef int (*rc_hook_pre_fn)(rc_ctx *c, void *user);
typedef void (*rc_hook_post_fn)(rc_ctx *c, void *user);

// Adds hooks to the hook point `name` (tools/targets/names/hooks.list); either may be NULL.
// Returns 0, or -1 if the build has no hook point of that name or its hooks are full.
int rc_hook_add_name(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user);
// The same by image address (unslid): this target's address of a hook point, or the mac-x64 address of one.
int rc_hook_add(uint64_t vmaddr, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user);
// 1 if the build has a hook point at vmaddr
int rc_hook_exists(uint64_t vmaddr);
// The game this app was built from: "mac-x64" (the macOS game); "unknown" in a build without the image.
const char *rc_target(void);

// One hook point by name (the generated rc_target.c lists them): this target's address and the mac-x64 one.
typedef struct {
  const char *name;
  uint64_t vmaddr, mac_vmaddr;
} rc_hook_name;

// The generated wrappers' entry
void rc_hook_call(int index, rc_ctx *c, rc_fn orig);

#endif
