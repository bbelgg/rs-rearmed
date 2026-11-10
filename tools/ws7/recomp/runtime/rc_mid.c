// Edits and mid points (rc_mid.h): the switches the generated code reads, and the mid points' hooks.
#include "rc_mid.h"

#include <stdio.h>
#include <string.h>

#ifdef RC_GUEST32
#include "rc32.h"
#define PTR_SIZE 4
#else
#define PTR_SIZE 8
#endif

// Generated per target (tools/targets/targets.py gen: rc_target.c), which overrides these. Weak, as rc_hook.c's,
// so this file links without the hooks (the translator's tests).
__attribute__((weak)) const rc_edit_name rc_target_edits[1] = {{0}};
__attribute__((weak)) const rc_mid_name rc_target_mids[1] = {{0}};
__attribute__((weak)) const char rc_target_id[1] = "";
static const char *target(void) { return rc_target_id[0] ? rc_target_id : "unknown"; }

uint8_t rc_edit_on[RC_EDIT_GROUPS];
uint8_t rc_mid_on[RC_MID_POINTS];
static int g_fixed;

int rc_edit_enable(const char *group) {
  int k = 0;
  for (const rc_edit_name *e = rc_target_edits; group && e->name && k < RC_EDIT_GROUPS; e++, k++) {
    if (strcmp(e->name, group)) continue;
    if (!e->nedits) {
      fprintf(stderr, "[rc] edits %s: not on this game (%s): %s\n", group, target(), e->why);
      return -1;
    }
    if (g_fixed && !rc_edit_on[k]) {
      fprintf(stderr, "[rc] edits %s: too late, the game has started (switch a group on in a mod's constructor)\n",
              group);
      return -1;
    }
    if (!rc_edit_on[k]) fprintf(stderr, "[rc] edits %s: on (%d)\n", group, e->nedits);
    rc_edit_on[k] = 1;
    return 0;
  }
  fprintf(stderr, "[rc] edits %s: no group of that name (%s)\n", group ? group : "(null)", target());
  return -1;
}

int rc_edit_is_on(const char *group) {
  int k = 0;
  for (const rc_edit_name *e = rc_target_edits; group && e->name && k < RC_EDIT_GROUPS; e++, k++)
    if (!strcmp(e->name, group)) return rc_edit_on[k];
  return 0;
}

void rc_edits_fix(void) { g_fixed = 1; }

// --- mid points ------------------------------------------------------------------------------------------------
#define MAX_MID_HOOKS 4
typedef struct {
  rc_mid_fn fn[MAX_MID_HOOKS];
  void *user[MAX_MID_HOOKS];
  int n;
} mid_slot;
static mid_slot g_mid[RC_MID_POINTS];

// The table's entry of a point. A hook reads its values through this (rc_mid_get: value_of), on whatever path its
// point is on, so the table is counted once and indexed, not walked for each value. The count is the same from
// any thread.
static const rc_mid_name *mid_of(int point) {
  static int n = -1;
  int have = __atomic_load_n(&n, __ATOMIC_RELAXED);
  if (have < 0) {
    for (have = 0; have < RC_MID_POINTS && rc_target_mids[have].name;) have++;
    __atomic_store_n(&n, have, __ATOMIC_RELAXED);
  }
  return point >= 0 && point < have ? &rc_target_mids[point] : 0;
}

int rc_mid_add_name(const char *name, rc_mid_fn fn, void *user) {
  int k = 0;
  for (const rc_mid_name *m = rc_target_mids; name && m->name; m++, k++) {
    if (strcmp(m->name, name)) continue;
    if (m->none) {  // nothing to do at this moment on this game: taken, never called (rc_mid_on stays 0)
      if (!fn) {
        fprintf(stderr, "[rc] mid point %s: no function given\n", name);
        return -1;
      }
      fprintf(stderr, "[rc] mid point %s: not needed on this game (%s): %s\n", name, target(), m->why ? m->why : "");
      return k;
    }
    if (!m->vmaddr) {
      fprintf(stderr, "[rc] mid point %s: not on this game (%s): %s\n", name, target(), m->why);
      return -1;
    }
    if (k >= RC_MID_POINTS || g_mid[k].n == MAX_MID_HOOKS) {
      fprintf(stderr, "[rc] mid point %s: its %d hooks are taken\n", name, MAX_MID_HOOKS);
      return -1;
    }
    if (!fn) {
      fprintf(stderr, "[rc] mid point %s: no function given\n", name);
      return -1;
    }
    g_mid[k].user[g_mid[k].n] = user;
    g_mid[k].fn[g_mid[k].n++] = fn;
    rc_mid_on[k] = 1;
    return k;
  }
  fprintf(stderr, "[rc] mid point %s: no point of that name (%s)\n", name ? name : "(null)", target());
  return -1;
}

int rc_mid_is_none(int point) {
  const rc_mid_name *m = mid_of(point);
  return m && m->none;
}

int rc_mid_value(int point, const char *value) {
  const rc_mid_name *m = mid_of(point);
  if (m && m->none) return -1;  // no values: its hooks never run
  for (int i = 0; m && value && i < m->nvalues; i++)
    if (!strcmp(m->value[i].name, value)) return i;
  fprintf(stderr, "[rc] mid point %s: no value %s here (%s)\n", m ? m->name : "?", value ? value : "(null)", target());
  return -1;
}

int rc_mid_cont(int point, const char *cont) {
  const rc_mid_name *m = mid_of(point);
  if (m && m->none) return -1;
  for (int i = 0; m && cont && i < m->nconts; i++)
    if (!strcmp(m->cont[i], cont)) return i + 1;
  fprintf(stderr, "[rc] mid point %s: no continuation %s here (%s)\n", m ? m->name : "?", cont ? cont : "(null)",
          target());
  return -1;
}

// The registers rc_mid_set wrote in the running callbacks of this thread's innermost point: bit k for r[k], 16 + k
// for x[k]
static __thread uint32_t t_set;

int rc_mid_call(int point, rc_ctx *c, uint32_t *flags) {
  mid_slot *s = &g_mid[point];
  rc_mid_frame f = {c, point, *flags, 0};
  // A callback's calls into the game (OV_GUEST, rc_call_name) leave the registers as the callee did, eax, ecx and edx
  // (the SysV call-clobbered ones on the Mac) among them, and the code after the point reads them as its own: after
  // the callbacks, every register is as the point found it, but those a callback set (rc_mid_set, rc_mid_set_f)
  uint64_t r[16];
  rc_v128 x[16];
  memcpy(r, c->r, sizeof r);
  memcpy(x, c->x, sizeof x);
  const uint32_t outer = t_set;
  t_set = 0;
  for (int i = 0; i < s->n; i++) s->fn[i](&f, s->user[i]);
  for (int k = 0; k < 16; k++) {
    if (!(t_set >> k & 1)) c->r[k] = r[k];
    if (!(t_set >> (16 + k) & 1)) c->x[k] = x[k];
  }
  t_set = outer;
  *flags = f.flags;
  const rc_mid_name *m = f.go ? mid_of(point) : 0;
  if (m && (f.go < 0 || f.go > m->nconts)) {
    fprintf(stderr, "[rc] mid point %s: no continuation %d, the instruction runs\n", m->name, f.go);
    return 0;
  }
  return f.go;
}

// --- values ------------------------------------------------------------------------------------------------------
// Host address of guest address g, and back
static void *host(uint64_t g) {
#ifdef RC_GUEST32
  return RC32_H(g);
#else
  return (void *)(uintptr_t)g;
#endif
}

static const rc_mid_val *value_of(const rc_mid_frame *f, int value) {
  const rc_mid_name *m = mid_of(f->point);
  if (!m || value < 0 || value >= m->nvalues) {
    fprintf(stderr, "[rc] mid point %s: no value %d\n", m ? m->name : "?", value);
    return 0;
  }
  return &m->value[value];
}

static int width(char kind) {
  switch (kind) {
    case 'b': return 1;
    case 'h': return 2;
    case 'i': case 'f': return 4;
    case 'p': return PTR_SIZE;
    default: return 8;
  }
}

// The memory a value is in: a general register's word in rc_ctx, or guest memory
static void *where(const rc_mid_frame *f, const rc_mid_val *v) {
  if (v->loc & RC_MID_MEM) {
    uint64_t base = f->c->r[v->loc & 15];
#ifdef RC_GUEST32
    return host((uint32_t)(base + (int64_t)v->off));
#else
    return host(base + (int64_t)v->off);
#endif
  }
  if (v->loc < 16) return &f->c->r[v->loc];
  if (v->loc < 32) return &f->c->x[v->loc - 16];
  return 0;
}

uint64_t rc_mid_get(const rc_mid_frame *f, int value) {
  const rc_mid_val *v = value_of(f, value);
  void *p = v ? where(f, v) : 0;
  if (!p || v->kind == 'f' || v->kind == 'd') return 0;
  uint64_t x = 0;
  memcpy(&x, p, width(v->kind));  // little-endian: the low bytes of a register
  if (v->kind == 'p' && PTR_SIZE == 4) return x ? (uint64_t)(uintptr_t)host((uint32_t)x) : 0;
  return x;
}

void rc_mid_set(rc_mid_frame *f, int value, uint64_t x) {
  const rc_mid_val *v = value_of(f, value);
  void *p = v ? where(f, v) : 0;
  if (!p || v->kind == 'f' || v->kind == 'd') return;
#ifdef RC_GUEST32
  if (v->kind == 'p' && x) {
    uint64_t lo = (uint64_t)(uintptr_t)rc_mem;
    if (x < lo || x - lo > 0xffffffffull) {
      const rc_mid_name *m = mid_of(f->point);
      fprintf(stderr, "[rc] mid point %s: value %s: a pointer outside the game's memory (rc_guest_alloc), null instead\n",
              m ? m->name : "?", v->name);
      x = 0;
    } else {
      x -= lo;
    }
  }
#endif
  int n = width(v->kind);
  if (!(v->loc & RC_MID_MEM) && v->loc < 32) t_set |= 1u << v->loc;
  // A 32-bit value in a 64-bit register zero-extends it, as a write to eax does; a byte or a halfword keeps the rest
  if (!(v->loc & RC_MID_MEM) && v->loc < 16 && n == 4) n = 8, x = (uint32_t)x;
  memcpy(p, &x, n);
}

double rc_mid_get_f(const rc_mid_frame *f, int value) {
  const rc_mid_val *v = value_of(f, value);
  if (!v || (v->kind != 'f' && v->kind != 'd')) return 0;
#ifdef RC_GUEST32
  if (!(v->loc & RC_MID_MEM) && v->loc >= 32) return f->c->fpu.st[(f->c->fpu.top + v->loc - 32) & 7];
#endif
  void *p = where(f, v);
  if (!p) return 0;
  if (v->kind == 'f') {
    float x;
    memcpy(&x, p, 4);
    return x;
  }
  double x;
  memcpy(&x, p, 8);
  return x;
}

void rc_mid_set_f(rc_mid_frame *f, int value, double x) {
  const rc_mid_val *v = value_of(f, value);
  if (!v || (v->kind != 'f' && v->kind != 'd')) return;
#ifdef RC_GUEST32
  if (!(v->loc & RC_MID_MEM) && v->loc >= 32) {
    f->c->fpu.st[(f->c->fpu.top + v->loc - 32) & 7] = v->kind == 'f' ? (double)(float)x : x;
    return;
  }
#endif
  void *p = where(f, v);
  if (!p) return;
  if (!(v->loc & RC_MID_MEM) && v->loc < 32) t_set |= 1u << v->loc;
  if (v->kind == 'f') {
    float y = (float)x;
    memcpy(p, &y, 4);
  } else {
    memcpy(p, &x, 8);
  }
}
