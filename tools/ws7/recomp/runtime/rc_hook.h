// Hook points: guest functions the build wraps (gen_image.py, skip-list lines `ADDR wrap`, listed in
// overrides/hooks.skip) so that native code loaded at run time (the mods, app/rc_main.c) can run before and after
// them. The executable exports these functions and the runtime (rc_call_guest, rc_slide, ...) to the mods.
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

// Adds hooks to the hook point at image address vmaddr (unslid); either may be NULL.
// Returns 0, or -1 if vmaddr is not a hook point of this build or its hooks are full.
int rc_hook_add(uint64_t vmaddr, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user);
// 1 if the build has a hook point at vmaddr
int rc_hook_exists(uint64_t vmaddr);

// The generated wrappers' entry
void rc_hook_call(int index, rc_ctx *c, rc_fn orig);

#endif
