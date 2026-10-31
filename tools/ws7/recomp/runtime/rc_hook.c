// Hook points (rc_hook.h).
#include "rc_hook.h"

#include <string.h>

// Generated (table.c), which overrides these; a build without the image (app/build.sh --stub) has none.
__attribute__((weak)) const uint64_t rc_hook_points[1] = {0};
__attribute__((weak)) const int rc_hook_npoints = 0;
// Generated per target (tools/targets/targets.py gen: rc_target.c), which overrides these.
__attribute__((weak)) const rc_hook_name rc_target_hooks[1] = {{0, 0, 0}};
__attribute__((weak)) const char rc_target_id[1] = "";

#define MAX_POINTS 256
#define MAX_HOOKS 8

typedef struct {
  rc_hook_pre_fn pre[MAX_HOOKS];
  void *pre_user[MAX_HOOKS];
  rc_hook_post_fn post[MAX_HOOKS];
  void *post_user[MAX_HOOKS];
  int npre, npost;
} slot;
static slot g_slots[MAX_POINTS];

static int index_of(uint64_t vmaddr) {
  for (int i = 0; i < rc_hook_npoints && i < MAX_POINTS; i++)
    if (rc_hook_points[i] == vmaddr) return i;
  return -1;
}

int rc_hook_exists(uint64_t vmaddr) { return index_of(vmaddr) >= 0; }

const char *rc_target(void) { return rc_target_id[0] ? rc_target_id : "unknown"; }

int rc_hook_add(uint64_t vmaddr, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user) {
  int i = index_of(vmaddr);
  for (const rc_hook_name *h = rc_target_hooks; i < 0 && h->name; h++)  // a mac-x64 address on another target
    if (h->mac_vmaddr == vmaddr) i = index_of(h->vmaddr);
  if (i < 0) return -1;
  slot *s = &g_slots[i];
  if ((pre && s->npre == MAX_HOOKS) || (post && s->npost == MAX_HOOKS)) return -1;
  if (pre) {
    s->pre_user[s->npre] = user;
    s->pre[s->npre++] = pre;
  }
  if (post) {
    s->post_user[s->npost] = user;
    s->post[s->npost++] = post;
  }
  return 0;
}

int rc_hook_add_name(const char *name, rc_hook_pre_fn pre, rc_hook_post_fn post, void *user) {
  for (const rc_hook_name *h = rc_target_hooks; name && h->name; h++)
    if (!strcmp(h->name, name)) return rc_hook_add(h->vmaddr, pre, post, user);
  return -1;
}

void rc_hook_call(int index, rc_ctx *c, rc_fn orig) {
  slot *s = &g_slots[index];
  for (int i = 0; i < s->npre; i++)
    if (s->pre[i](c, s->pre_user[i])) return;
  orig(c);
  for (int i = 0; i < s->npost; i++) s->post[i](c, s->post_user[i]);
}
