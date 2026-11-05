// Hook points (rc_hook.h).
#include "rc_hook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_rt.h"

// Generated (table.c), which overrides these; a build without the image (app/build.sh --stub) has none.
__attribute__((weak)) const uint64_t rc_hook_points[1] = {0};
__attribute__((weak)) const int rc_hook_npoints = 0;
// Generated per target (tools/targets/targets.py gen: rc_target.c), which overrides these.
__attribute__((weak)) const rc_hook_name rc_target_hooks[1] = {{0, 0, 0}};
__attribute__((weak)) const rc_target_value rc_target_offsets[1] = {{0, 0}};
__attribute__((weak)) const rc_target_value rc_target_symbols[1] = {{0, 0}};
__attribute__((weak)) const char rc_target_id[1] = "";

#ifdef RC_GUEST32
#include "rc32_hook.h"
const int rc_guest_ptr_size = 4;
#else
const int rc_guest_ptr_size = 8;
#endif

#define MAX_POINTS 256
#define MAX_HOOKS 8

typedef struct {
  rc_hook_pre_fn pre[MAX_HOOKS];
  void *pre_user[MAX_HOOKS];
  rc_hook_post_fn post[MAX_HOOKS];
  void *post_user[MAX_HOOKS];
  int npre, npost;
#ifdef RC_GUEST32
  const rc_hook_name *abi;
  int pops;
  const char *trace;  // RC_HOOK_TRACE: this point's name
  int traced;
#endif
} slot;
static slot g_slots[MAX_POINTS];
#ifdef RC_GUEST32
uint8_t rc_hook_on[MAX_POINTS];
#endif

static int index_of(uint64_t vmaddr) {
  for (int i = 0; i < rc_hook_npoints && i < MAX_POINTS; i++)
    if (rc_hook_points[i] == vmaddr) return i;
  return -1;
}

int rc_hook_exists(uint64_t vmaddr) { return index_of(vmaddr) >= 0; }

const char *rc_target(void) { return rc_target_id[0] ? rc_target_id : "unknown"; }

__attribute__((weak)) int rc_hook_add_seam(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user) {
  (void)name; (void)pre; (void)post; (void)user;
  return -1;
}

int rc_hook_add(uint64_t vmaddr, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user) {
  int i = index_of(vmaddr);
  const rc_hook_name *h = rc_target_hooks, *found = 0;
  for (; h->name && !found; h++)  // this target's address, or a mac-x64 address on another target
    if (h->vmaddr == vmaddr || h->mac_vmaddr == vmaddr) found = h;
  if (i < 0 && found) i = index_of(found->vmaddr);
  if (i < 0) {
    if (found && !found->vmaddr && !rc_hook_add_seam(found->name, pre, post, user)) return 0;
    if (found && !found->vmaddr)
      fprintf(stderr, "[rc] hook %s: not on this game (%s): %s\n", found->name, rc_target(), found->why);
    else
      fprintf(stderr, "[rc] hook: no hook point %s at 0x%llx in this build (%s)\n", found ? found->name : "of that name",
              (unsigned long long)vmaddr, rc_target());
    return -1;
  }
  slot *s = &g_slots[i];
#ifdef RC_GUEST32
  if (rc32_hook_shown(found, i, &s->pops)) return -1;
  s->abi = found;
#endif
  if ((pre && s->npre == MAX_HOOKS) || (post && s->npost == MAX_HOOKS)) {
    fprintf(stderr, "[rc] hook %s: its %d hooks are taken\n", found ? found->name : "?", MAX_HOOKS);
    return -1;
  }
  if (pre) {
    s->pre_user[s->npre] = user;
    s->pre[s->npre++] = pre;
  }
  if (post) {
    s->post_user[s->npost] = user;
    s->post[s->npost++] = post;
  }
#ifdef RC_GUEST32
  rc_hook_on[i] = 1;
#endif
  return 0;
}

int rc_hook_add_name(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user) {
  for (const rc_hook_name *h = rc_target_hooks; name && h->name; h++)
    if (!strcmp(h->name, name)) {
      if (h->vmaddr) return rc_hook_add(h->vmaddr, pre, post, user);
      if (!rc_hook_add_seam(name, pre, post, user)) return 0;
      fprintf(stderr, "[rc] hook %s: not on this game (%s): %s\n", name, rc_target(), h->why);
      return -1;
    }
  fprintf(stderr, "[rc] hook %s: no hook point of that name (%s)\n", name ? name : "(null)", rc_target());
  return -1;
}

#ifndef RC_GUEST32
void rc_hook_call(int index, rc_ctx *c, rc_fn orig) {
  slot *s = &g_slots[index];
  for (int i = 0; i < s->npre; i++)
    if (s->pre[i](c, s->pre_user[i])) return;
  orig(c);
  for (int i = 0; i < s->npost; i++) s->post[i](c, s->post_user[i]);
}
#else
// The hooks run on a view of the call (rc32_hook.c, rc_hook.h)
void rc_hook_call(int index, rc_ctx *c, rc_fn orig) {
  slot *s = &g_slots[index];
  int traced = s->trace && s->traced < RC_HOOK_TRACE_CALLS ? ++s->traced : 0;
  if (traced) rc32_trace_in(s->trace, traced, c);
  if (!s->npre && !s->npost) {
    orig(c);
    if (traced) rc32_trace_out(s->trace, traced, c);
    return;
  }
  rc32_view w;
  rc32_view_in(&w, s->abi, c);
  for (int i = 0; i < s->npre; i++)
    if (s->pre[i](&w.ctx, s->pre_user[i])) {
      rc32_view_return(&w, s->abi, s->pops, c);
      return;
    }
  rc32_view_args_out(&w, s->abi, c);
  orig(c);
  if (traced) rc32_trace_out(s->trace, traced, c);
  if (!s->npost) return;
  rc32_view_result_in(&w, s->abi, c);
  for (int i = 0; i < s->npost; i++) s->post[i](&w.ctx, s->post_user[i]);
  rc32_view_result_out(&w, s->abi, c);
}

void rc_hook_trace_init(void) {
  const char *e = getenv("RC_HOOK_TRACE");
  if (!e || !*e) return;
  for (const rc_hook_name *h = rc_target_hooks; h->name; h++) {
    size_t n = strlen(h->name);
    for (const char *p = strstr(e, h->name); p; p = strstr(p + 1, h->name))
      if ((p == e || p[-1] == ',') && (p[n] == ',' || !p[n]) && index_of(h->vmaddr) >= 0) {
        g_slots[index_of(h->vmaddr)].trace = h->name;
        rc_hook_on[index_of(h->vmaddr)] = 1;
        break;
      }
  }
}
#endif

// --- names --------------------------------------------------------------------------------------
static const rc_target_value *find_name(const rc_target_value *t, const char *name) {
  for (; name && t->name; t++)
    if (!strcmp(t->name, name)) return t;
  return 0;
}

int64_t rc_off(const char *name) {
  const rc_target_value *v = find_name(rc_target_offsets, name);
  if (v) return v->value;
  fprintf(stderr, "[rc] offset %s: not on this game (%s)\n", name ? name : "(null)", rc_target());
  return RC_NO_OFF;
}

uint64_t rc_addr(const char *name) {
  const rc_target_value *v = find_name(rc_target_symbols, name);
  if (v) return (uint64_t)v->value;
  fprintf(stderr, "[rc] symbol %s: not on this game (%s)\n", name ? name : "(null)", rc_target());
  return 0;
}

__attribute__((weak)) int rc_call_seam(const char *name, const uint64_t *ints, int nint) {
  (void)name; (void)ints; (void)nint;
  return -1;
}

uint64_t rc_call_name_f(const char *name, const uint64_t *args, int nargs, uint32_t floats) {
  const rc_target_value *v = find_name(rc_target_symbols, name);
  if (!v && !rc_call_seam(name, args, nargs)) return 0;
  if (!v || v->cc < 0) {
    fprintf(stderr, "[rc] call %s: no such function on this game (%s)\n", name ? name : "(null)", rc_target());
    return 0;
  }
#ifdef RC_GUEST32
  (void)floats;  // a float is a word on the stack like the others
  return rc32_call_cc((uint32_t)v->value, v->cc, name, args, nargs);
#else
  if (!floats) return rc_call_guest(RC_A((uint64_t)v->value), args, nargs, 0, 0, 0);
  uint64_t ints[16], dbls[8];
  int ni = 0, nd = 0;
  for (int k = 0; k < nargs; k++) {
    if (floats >> k & 1 && nd < 8) dbls[nd++] = args[k];
    else if (ni < 16) ints[ni++] = args[k];
  }
  return rc_call_guest(RC_A((uint64_t)v->value), ints, ni, dbls, nd, 0);
#endif
}

uint64_t rc_call_name(const char *name, const uint64_t *ints, int nint) { return rc_call_name_f(name, ints, nint, 0); }

#ifndef RC_GUEST32
void *rc_guest_alloc(size_t size) { return malloc(size); }
#endif
