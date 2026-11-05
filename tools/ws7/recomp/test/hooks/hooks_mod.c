// The mod the hook tests load: built once (no RC_GUEST32, no target tables), the same dylib in the tests of
// both targets, so what it reads is what a mod reads on each. test_hooks.c drives it through hm_*.
#include "rc_mod.h"

#define HM_POINTS 16
#define HM_ARGS 8
typedef struct {
  const char *name, *sig;  // the point and its arguments: p b h i q f d
  char ret;
} hm_point;
const hm_point hm_points[HM_POINTS] = {
    {"mix_cdecl", "pbhiqfd", 'i'},
    {"two_stdcall", "pq", 'q'},
    {"obj_thiscall", "pii", 'b'},
    {"fast_fastcall", "pii", 'p'},
    {"reg_custom", "ppi", 'f'},
    {"float_cdecl", "f", 'd'},
    {"many_cdecl", "iiiiiiii", 'i'},
    {"folded_stdcall", "pii", 'v'},
    {"hidden_thiscall", "", 'b'},
    {"ratio_cdecl", "f", 'f'},
    {0, 0, 0},
};

// What the test sets: 0 watch, 1 change the arguments, 2 do the call (result hm_result), 3 change the result in
// the post hook, 4 call a game function by name first (registers the original needs must survive it)
int hm_action;
uint64_t hm_result;
// What the hooks saw
uint64_t hm_seen[HM_POINTS][HM_ARGS], hm_seen_result[HM_POINTS], hm_ret_addr[HM_POINTS];
int hm_pre[HM_POINTS], hm_post[HM_POINTS];
int hm_refused[8];
int64_t hm_off, hm_no_off, hm_slot;
int hm_ptr_size;
uint64_t hm_addr;
const char *hm_target;
void *hm_read_ptr;
int hm_order;

static uint64_t to_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t to_bits_d(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static int pre(rc_ctx *c, void *user) {
  int id = (int)(intptr_t)user;
  const char *sig = hm_points[id].sig;
  hm_pre[id]++;
  hm_ret_addr[id] = RC_RET_ADDR(c);
  OV_ARGS(a, c);
  for (int k = 0; sig[k]; k++) {
    switch (sig[k]) {
      case 'p': hm_seen[id][k] = (uint64_t)(uintptr_t)OV_PTR(a, void); break;
      case 'q': hm_seen[id][k] = OV_U64(a); break;
      case 'f': hm_seen[id][k] = to_bits(OV_F32(a)); break;
      case 'd': hm_seen[id][k] = to_bits_d(OV_F64(a)); break;
      default: hm_seen[id][k] = OV_U32(a); break;
    }
  }
  if (hm_action == 1) {
    static const uint8_t order[6] = {7, 6, 2, 1, 8, 9};
    int gi = 0, fi = 0;
    for (int k = 0; sig[k]; k++) {
      if (sig[k] == 'f') c->x[fi++][0] = to_bits(2.5f);
      else if (sig[k] == 'd') c->x[fi++][0] = to_bits_d(-4.25);
      else if (sig[k] == 'p') gi++;  // stays: the test gives the changed pointer below
      else if (gi < 6) c->r[order[gi++]] += 1;
      else *(uint64_t *)(uintptr_t)(c->r[4] + 8 * (uint64_t)(++gi - 6)) += 1;
    }
    if (sig[0] == 'p' && c->r[7]) c->r[7] += 4;
  }
  if (hm_action == 4) {
    uint64_t v[2] = {7, 8};
    rc_call_name("obj_thiscall", v, 2);
  }
  if (hm_action == 2) {
    switch (hm_points[id].ret) {
      case 'f': ov_ret_f32(c, 1.5f); break;
      case 'd': ov_ret_f64(c, 2.25); break;
      default: ov_ret_u64(c, hm_result); break;
    }
    return 1;
  }
  return 0;
}

static void post(rc_ctx *c, void *user) {
  int id = (int)(intptr_t)user;
  char ret = hm_points[id].ret;
  hm_post[id]++;
  hm_seen_result[id] = ret == 'f' || ret == 'd' ? c->x[0][0] : c->r[0];
  if (hm_action == 3) {
    if (ret == 'f') rc_ret_f32(c, 8.5f);
    else if (ret == 'd') rc_ret_f64(c, 16.5);
    else c->r[0] = hm_result;
  }
}

static int second(rc_ctx *c, void *user) {
  (void)c;
  (void)user;
  hm_order = hm_order * 10 + 2;
  return 0;
}
static int first(rc_ctx *c, void *user) {
  (void)c;
  (void)user;
  hm_order = hm_order * 10 + 1;
  return 0;
}

__attribute__((constructor)) static void init(void) {
  for (int k = 0; hm_points[k].name; k++)
    if (rc_hook_add_name(hm_points[k].name, pre, post, (void *)(intptr_t)k)) hm_refused[7]++;
  rc_hook_add_name("many_cdecl", first, 0, 0);
  rc_hook_add_name("many_cdecl", second, 0, 0);
  hm_refused[0] = rc_hook_add_name("not_on_this_game", pre, post, 0);
  hm_refused[1] = rc_hook_add_name("no_such_point", pre, post, 0);
  hm_refused[2] = rc_hook_add_name("regs_unsaid", pre, post, 0);
  hm_refused[3] = rc_hook_add_name("wrong_size", pre, post, 0);
  hm_refused[4] = rc_hook_add_name("not_the_macs", pre, post, 0);
  hm_off = RC_OFF(fld);
  hm_slot = RC_SLOT(fn);
  hm_no_off = rc_off("no_such_field");
  hm_addr = RC_ADDR(glob);
  hm_ptr_size = rc_guest_ptr_size;
  hm_target = rc_target();
}

// Names this game hasn't, asked for at one place each: called again, a place doesn't look or log again
void hm_missing(int *has, int64_t *slot, uint64_t *addr) {
  *has = RC_HAS_OFF(no_such_field_once);
  *slot = RC_SLOT(no_such_fn_once);
  *addr = RC_ADDR(no_such_symbol_once);
}

// A guest pointer of this target's width at p, and back
void *hm_ptr_at(const void *p) { return rc_mod_ptr(p); }
void *hm_host(uint64_t g) { return RC_HOST(g); }
uint64_t hm_guest(const void *h) { return RC_GUEST(h); }
