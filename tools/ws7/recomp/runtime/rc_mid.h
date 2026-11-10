// Inside a function: edits and mid points, for the mods. Where a hook point (rc_hook.h) runs native code around a
// whole function, these reach into one:
//
//   * An edit group (names/edits.list, each target's edits.map: `GROUP ADDRESS OLDHEX NEWHEX`) is instructions the
//     build translated twice, as the game has them and as the table changes them (a stride, a count, an offset, a
//     branch). The game runs its own until a mod switches the group on with rc_edit_enable, in its constructor:
//     the switches are fixed when the mods folder has loaded, before the game runs, so no object the game makes
//     sees both. A group the target hasn't is refused with a log line.
//
//   * A mid point (names/mids.list, each target's mids.map: `NAME ADDRESS (TYPE value@LOCATION, ...)
//     [-> CONT=ADDRESS, ...]`) runs a mod's function before the instruction at ADDRESS. It gets the guest's
//     registers and flags at that point, reads and changes the point's values by name (rc_mid_get, rc_mid_set: a
//     register, an x87 or SSE register, or memory at a register plus an offset, as the table says for this target),
//     and either lets the instruction run (go = 0) or goes to one of the point's continuations (go = n, the n-th of
//     the table's list, rc_mid_cont). A point without hooks costs a load and a branch. A hook may call the game
//     (OV_GUEST), which changes registers as any call does: after the hooks, every register (and xmm register) is
//     as the point found it unless a hook set it with rc_mid_set or rc_mid_set_f.
//
// A point can be `none` on a game (mids.map: `NAME none REASON`): that game needs no call at that moment (its code
// reaches the same result another way, an edit say). A hook on it is taken (rc_mid_add_name answers its index) and
// never called; rc_mid_is_none tells a mod so, and rc_mid_value / rc_mid_cont answer -1 on it without a log line.
//
// A value is what its type says on every target: an integer of its width (zero-extended), a pointer as a host
// pointer (null stays null; one given back must be in the guest's memory, rc_guest_alloc), a float or double. The
// same mod source serves every target as long as the tables of each give a point the same values and continuations.
#ifndef RC_MID_H
#define RC_MID_H

#include <stdint.h>

#include "rc.h"

// --- edits ---------------------------------------------------------------------------------------------------
// Switches a group to its edited instructions for the whole run. In a mod's constructor; 0, or -1 with a log line
// (no such group, none on this game, or the game has started).
int rc_edit_enable(const char *group);
// 1 when the group is on
int rc_edit_is_on(const char *group);

// --- mid points ----------------------------------------------------------------------------------------------
typedef struct rc_mid_frame {
  rc_ctx *c;       // the guest's registers: through rc_mid_get / rc_mid_set
  int point;       // the point (rc_mid_add_name's answer)
  uint32_t flags;  // EFLAGS as the instruction finds them: CF bit 0, PF 2, ZF 6, SF 7, OF 11; written back
  int go;          // 0: the point's instruction runs; n: on at the point's n-th continuation instead
} rc_mid_frame;
typedef void (*rc_mid_fn)(rc_mid_frame *f, void *user);

// Adds fn to the point `name`: its index, or -1 with a log line (no such point, not on this game, its hooks taken).
// Before the game runs, as hooks. On a point that is none here: its index, one log line, and fn is never called.
int rc_mid_add_name(const char *name, rc_mid_fn fn, void *user);
// 1 when the point (rc_mid_add_name's answer) is none on this game, else 0
int rc_mid_is_none(int point);
// A value's index by its name in the table, and a continuation's number (for go): -1 with a log line for a name the
// point hasn't here.
int rc_mid_value(int point, const char *value);
int rc_mid_cont(int point, const char *cont);
// A value: an integer zero-extended, a pointer as a host pointer; a float or double in rc_mid_get_f.
uint64_t rc_mid_get(const rc_mid_frame *f, int value);
void rc_mid_set(rc_mid_frame *f, int value, uint64_t v);
double rc_mid_get_f(const rc_mid_frame *f, int value);
void rc_mid_set_f(rc_mid_frame *f, int value, double v);

// --- the tables (tools/targets/targets.py gen: rc_target.c) ----------------------------------------------------
#define RC_MID_MAXVALUES 8
#define RC_MID_MAXCONTS 8
#define RC_MID_MEM 0x80  // loc: memory at the register plus off
// loc: 0-15 a general register (x86 encoding order), 16-31 xmm0-15, 32-39 st0-7 (counted from the x87 top);
// kind as rc_hook_arg's: b h i q integers, p pointer, f float, d double
typedef struct {
  const char *name;
  char kind;
  uint8_t loc;
  int32_t off;
} rc_mid_val;
typedef struct {
  const char *name;
  uint64_t vmaddr;  // 0: not on this game, for `why`
  const char *why;
  int8_t nvalues;
  rc_mid_val value[RC_MID_MAXVALUES];
  int8_t nconts;
  const char *cont[RC_MID_MAXCONTS];
  uint8_t none;     // 1: needs no call on this game (vmaddr 0, `why` says why); hooks are taken, never called
} rc_mid_name;
typedef struct {
  const char *name;
  int nedits;       // 0: none on this game, for `why`
  const char *why;
} rc_edit_name;

// --- what the generated code uses ------------------------------------------------------------------------------
#define RC_EDIT_GROUPS 64
#define RC_MID_POINTS 1024
extern uint8_t rc_edit_on[RC_EDIT_GROUPS];
extern uint8_t rc_mid_on[RC_MID_POINTS];
// Runs the point's hooks on c (the registers spilled) and *flags; returns the continuation (0: none)
int rc_mid_call(int point, rc_ctx *c, uint32_t *flags);
// The end of the mods folder's loading (rc_mods.c): the edit switches are fixed from here on
void rc_edits_fix(void);

#endif
