// The view a hook sees of a 32-bit guest's call (rc_hook.h), for rc_hook.c.
#ifndef RC32_HOOK_H
#define RC32_HOOK_H
#include "rc32.h"
#include "rc_cc.h"
#include "rc_hook.h"

typedef struct {
  rc_ctx ctx;                           // what the hooks are given
  uint64_t frame[1 + RC_HOOK_MAXARGS];  // what ctx's stack pointer is on: the return address, then what
                                        // SysV passes on the stack
  uint8_t at[RC_HOOK_MAXARGS];          // where in ctx or frame each argument is
  uint64_t in[RC_HOOK_MAXARGS];         // and its value there at entry
  uint64_t regs[8];                     // the guest's registers at entry (hooks may run guest code)
  uint64_t result;
  uint32_t ret_flags;
} rc32_view;

// 0 when hooks on point `index` (h: its table entry) can be shown the call, and *pops has what the function
// removes; else -1 with a log line.
int rc32_hook_shown(const rc_hook_name *h, int index, int *pops);
int rc_hook_code_pops(int index);
void rc32_view_in(rc32_view *w, const rc_hook_name *h, rc_ctx *c);
// A pre hook did the call itself: its result into the guest's registers, and the guest's `ret N`
void rc32_view_return(rc32_view *w, const rc_hook_name *h, int pops, rc_ctx *c);
// The original runs next: the guest's registers as they were, with what the pre hooks changed
void rc32_view_args_out(rc32_view *w, const rc_hook_name *h, rc_ctx *c);
void rc32_view_result_in(rc32_view *w, const rc_hook_name *h, rc_ctx *c);
void rc32_view_result_out(rc32_view *w, const rc_hook_name *h, rc_ctx *c);
void rc32_trace_in(const char *name, int n, rc_ctx *c);
void rc32_trace_out(const char *name, int n, rc_ctx *c);
uint64_t rc32_call_cc(uint32_t g, int cc, const char *name, const uint64_t *ints, int nint);

#endif
