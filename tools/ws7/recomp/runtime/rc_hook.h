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
//
// That picture is the same on every target: a hook always sees the Mac's call. On a guest with another
// ABI (win-x86, rc32_hook.c) c is not the guest's context but a view of the call made from the target's
// tables: the arguments in the SysV registers whatever the function's convention, a guest pointer as a
// host pointer (null stays null), a float in xmm0 and not on the stack, the stack pointer on a host copy of
// the return address. What a hook changes goes back: an argument changed by a pre hook, the result it or a
// post hook leaves in c->r[0] / c->x[0] (to eax, edx:eax or st0, as the function's prototype says), and the
// `ret N` is done for it. A hook point of a name is the Mac's call on every target: what differs is adapted in
// its prototype in symbols.map, or the point is refused. A function with its own register arguments
// (`custom`) says where they are (`const char *path@ecx`), an argument the compiler folded away has its one
// value (`int event=0x98`), one the Mac's call hasn't is hidden, a result of another form is converted.
// rc_hook_add refuses, with a log line, what can't be shown so: a call that isn't the Mac's (targets.py compares
// them), a function whose prototype doesn't say, stack arguments that don't add up to its own `ret N` (a stdcall,
// thiscall or fastcall function's: a cdecl or custom one's `ret` says nothing of them, and only its table does).
// A mod that follows rc_mod.h is one binary for every target.
#ifndef RC_HOOK_H
#define RC_HOOK_H

#include <stddef.h>
#include <stdint.h>

#include "rc.h"

typedef int (*rc_hook_pre_fn)(rc_ctx *c, void *user);
typedef void (*rc_hook_post_fn)(rc_ctx *c, void *user);

// Adds hooks to the hook point `name` (tools/targets/names/hooks.list); either may be NULL.
// Returns 0, or -1 with a log line if the build has no hook point of that name (this game may have none:
// `unsupported` in its symbols.map), its hooks are full, or its arguments can't be shown (above).
int rc_hook_add_name(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user);
// The same by image address (unslid): this target's address of a hook point, or the mac-x64 address of one.
int rc_hook_add(uint64_t vmaddr, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user);
// 1 if the build has a hook point at vmaddr
int rc_hook_exists(uint64_t vmaddr);
// The game this app was built from: "mac-x64" (the macOS game), "win-x86" (Learn & Play); "unknown" in a
// build without the image.
const char *rc_target(void);

// Names for code built once for every target (rc_mod.h has the macros): the value of a name in this
// target's offsets.map and symbols.map, with a log line and RC_NO_OFF / 0 for a name this target doesn't have.
#define RC_NO_OFF INT64_MIN
int64_t rc_off(const char *name);
uint64_t rc_addr(const char *name);  // unslid; the host address is RC_A(rc_addr(...))
// Calls the game function `name` with integer arguments by its convention; a guest pointer may be given as
// a host pointer. Returns eax / rax, as the guest has it (rc_mod.h: RC_HOST). 0 with a log line for a name
// this target doesn't have, or a `custom` function.
uint64_t rc_call_name(const char *name, const uint64_t *ints, int nint);
// The same with floats among the arguments: bit k of `floats` says args[k] is a float's bits, which go where this
// target's convention has them (xmm, or a word on the stack in its place).
uint64_t rc_call_name_f(const char *name, const uint64_t *args, int nargs, uint32_t floats);
// Memory the game can be given, never freed: a string or a result of a call into it. A mod's own memory is out of
// a 32-bit guest's reach. A host pointer; 0 when there is none.
void *rc_guest_alloc(size_t size);
// Bytes of a guest pointer: 8, or 4 on a 32-bit guest
extern const int rc_guest_ptr_size;

// The mods folder (rc_mods.c), in the app's main: every .dylib of a folder in name order, or one library,
// with `[rc] mod loaded: NAME` for each.
// What to load: RS_MODS_PATH, else dflt; NULL with RS_LOAD_MODS=0.
const char *rc_mods_where(const char *dflt);
int rc_mods_count(const char *where);  // how many rc_mods_load would try
void rc_mods_load(const char *where);

// One hook point by name (the generated rc_target.c lists them): this target's address and the mac-x64 one;
// on a target without the point, address 0 and why. The rest is for a guest with another ABI (SysV: left 0):
// cc is RC_CC_*, ret and arg[].kind say b h i q 8- to 64-bit integer, p pointer, f float, d double (v: no
// result), c an argument this target's code doesn't take, the same `value` in every call (`int event=0x98`),
// x one the Mac's call doesn't have, not shown; e a bool that is the u32 at ret_off of the first argument being
// ret_is, r a float the Mac has as 100 / this one. arg[].at is the word above the return address, or ~register
// (x86 encoding), pops what the callee removes (-1: unknown); nargs -1 when hooks are refused, for `why`.
#define RC_HOOK_MAXARGS 8
typedef struct {
  char kind;
  int8_t at;
  int32_t value;
} rc_hook_arg;
typedef struct {
  const char *name;
  uint64_t vmaddr, mac_vmaddr;
  const char *why;
  uint8_t cc;
  char ret;
  int16_t pops;
  int8_t nargs;
  rc_hook_arg arg[RC_HOOK_MAXARGS];
  int32_t ret_off, ret_is;
} rc_hook_name;
// rc_target.c's tables of offsets.map and symbols.map, sorted by name; `cc` is a symbol's RC_CC_*, -1 if
// it is not a function.
typedef struct {
  const char *name;
  int64_t value;
  int cc;
} rc_target_value;

// A point of the target's table that has no function in this game, run by the platform layer instead (Learn &
// Play's input_sink and set_app_focus, win32_input.c). 0 when the layer has taken the hooks.
int rc_hook_add_seam(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user);
// A call to such a point, as rc_call_name would make it (Learn & Play: input_sink feeds the input layer). 0 when
// the layer has it.
int rc_call_seam(const char *name, const uint64_t *ints, int nint);

// The generated wrappers' entry
void rc_hook_call(int index, rc_ctx *c, rc_fn orig);
#ifdef RC_GUEST32
// 1 when point `index` has hooks: the generated wrappers call the function directly otherwise
extern uint8_t rc_hook_on[];
// RC_HOOK_TRACE=name,name (diagnostics, checking a table): the raw entry and result of those points, the first
// RC_HOOK_TRACE_CALLS calls of each, refused ones too. Before the game runs.
#define RC_HOOK_TRACE_CALLS 8
void rc_hook_trace_init(void);
#endif

#endif
