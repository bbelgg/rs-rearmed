// The win-x86 offline online layer through the guest ABI, without the game: the vtables of a
// layout made in guest memory, the patched slots called as MSVC thiscall (this in ecx, the callee
// removes its stack words), and the game functions the layer calls (the event method, the
// original GetConfigBool and StartSignIn) as "guest" functions: C functions in rc32_lookup's
// table at fake addresses that behave like MSVC thiscall code (`ret 4 * n`).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "online32.h"
#include "win32_guestobj.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static uint32_t rd(uint32_t g) { return w32_rd32(g); }
static void guest_ret(rc_ctx *c, uint32_t eax, int words) {
  c->ret_to = rd((uint32_t)c->r[4]);
  c->r[4] += 4 + 4 * (uint32_t)words;
  c->r[0] = eax;
}
static uint32_t arg(rc_ctx *c, int k) { return rd((uint32_t)c->r[4] + 4 + 4 * (uint32_t)k); }

// --- fake game code -----------------------------------------------------------------------------
enum { G_EVENT = 0x1000, G_CONFIG = 0x1010, G_SIGN_IN = 0x1020, G_ORIG = 0x2000 };
static uint32_t gi_obj;
static char events[16][64];
static int nevents;
// The text of an inline efd string, checking its layout.
static const char *inline_text(uint32_t s) {
  CHECK(rd(s + 0x14) == s + 0x10);
  const char *t = (const char *)RC32_H(s);
  CHECK(rd(s + 0x10) == s + (uint32_t)strlen(t));
  return t;
}
static void f_event(rc_ctx *c) {  // slot 57: (const string &name, const string &param, Ptr extra), ret 0xc
  CHECK((uint32_t)c->r[1] == gi_obj);
  CHECK(arg(c, 2) == 0);
  CHECK(nevents < 16);
  snprintf(events[nevents++], sizeof events[0], "%s/%s", inline_text(arg(c, 0)), inline_text(arg(c, 1)));
  guest_ret(c, 0, 3);
}
static uint32_t config_this, config_key;
static void f_config(rc_ctx *c) {  // slot 67: GetConfigBool(const string &key, bool default): no config, the default
  config_this = (uint32_t)c->r[1];
  config_key = arg(c, 0);
  guest_ret(c, arg(c, 1) & 0xff, 2);
}
static void f_sign_in(rc_ctx *c) {  // StartSignIn(player, pad): starts once (state +0x2c 0 -> 1)
  uint32_t self = (uint32_t)c->r[1];
  uint32_t ok = rd(self + 0x2c) == 0 && arg(c, 0) <= 1 && arg(c, 1) <= 3;
  if (ok) w32_wr32(self + 0x2c, 1);
  guest_ret(c, ok, 2);
}
static const rc32_entry table[] = {{G_EVENT, f_event}, {G_CONFIG, f_config}, {G_SIGN_IN, f_sign_in}};
rc_fn rc32_lookup(uint32_t g) {
  for (size_t k = 0; k < sizeof table / sizeof table[0]; k++)
    if (table[k].addr == g) return table[k].fn;
  return 0;
}
int w32_log;  // win32_dispatch.c's LP_LOG (not linked)
int rc32_tls_images(const rc32_tls_image **out) { *out = 0; return 0; }
void rc32_hostfn_hook_add(rc32_hostfn_hook h) { (void)h; }
_Noreturn void rc_fail(const char *what, uint64_t addr) {
  fprintf(stderr, "rc_fail: %s at 0x%llx\n", what, (unsigned long long)addr);
  abort();
}

// --- calling as guest code would ---------------------------------------------------------------
static rc_ctx *C;
// thiscall through the object's vtable; checks the callee removed exactly n words
static uint32_t vcall(uint32_t obj, int slot, int n, const uint32_t *a) {
  uint32_t esp0 = (uint32_t)C->r[4];
  C->r[4] -= 4 * (uint32_t)n + 4;
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)C->r[4] + 4 + 4 * k, a[k]);
  w32_wr32((uint32_t)C->r[4], 0xdead0000u);  // return address
  C->r[1] = obj;
  int ok = w32_hostfn_call(C, rd(rd(obj) + 4 * slot));
  CHECK(ok);
  CHECK(C->ret_to == 0xdead0000u);
  CHECK((uint32_t)C->r[4] == esp0);
  return (uint32_t)C->r[0];
}
static uint32_t vtable(int n, uint32_t fill) {
  uint32_t v = w32_galloc(4 * (uint32_t)n);
  for (int k = 0; k < n; k++) w32_wr32(v + 4 * k, fill + 16 * (uint32_t)k);
  return v;
}
static uint32_t object(uint32_t vt, uint32_t size) {
  uint32_t o = w32_galloc(size);
  w32_wr32(o, vt);
  return o;
}
// A layout in guest memory: every slot a fake original, the three the layer calls real fakes.
static on32_layout make_layout(void) {
  on32_layout L;
  memset(&L, 0, sizeof L);
  L.ext_vtable = vtable(ON32_EXT_SLOTS, G_ORIG);
  for (int k = 0; k < ON32_EXT_SLOTS; k++) L.ext_slots[k] = G_ORIG + 16 * (uint32_t)k;
  L.gi_vtable = vtable(91, 0x3000);
  w32_wr32(L.gi_vtable + 4 * ON32_GI_SLOT_EVENT, G_EVENT);
  w32_wr32(L.gi_vtable + 4 * ON32_GI_SLOT_CONFIG_BOOL, G_CONFIG);
  L.gi_config_bool = G_CONFIG;
  L.gi_global = w32_galloc(4);
  L.profile_vtable = vtable(33, 0x4000);
  w32_wr32(L.profile_vtable + 4 * ON32_PROFILE_SLOT_SIGN_IN, G_SIGN_IN);
  L.profile_sign_in = G_SIGN_IN;
  return L;
}
static uint32_t efd_heap_string(const char *text) {  // > 15 chars: the text on the heap
  uint32_t s = w32_galloc(0x18), n = (uint32_t)strlen(text), h = w32_galloc(n + 1);
  memcpy(RC32_H(h), text, n + 1);
  w32_wr32(s, h);
  w32_wr32(s + 0x10, h + n);
  w32_wr32(s + 0x14, h + n + 1);
  return s;
}
static uint32_t efd_inline_string(const char *text) {
  uint32_t s = w32_galloc(0x18), n = (uint32_t)strlen(text);
  memcpy(RC32_H(s), text, n + 1);
  w32_wr32(s + 0x10, s + n);
  w32_wr32(s + 0x14, s + 0x10);
  return s;
}

int main(void) {
  CHECK(rc32_mem_init() == 0);
  C = rc32_thread_ctx();

  on32_layout L = make_layout();
  CHECK(on32_install(&L) == (ON32_EXT | ON32_CONFIG | ON32_SIGN_IN));
  CHECK(on32_install(&L) == 0);  // once

  // --- RSConnectExtension_RDV: IBase, the destructor and slot 27 stay the game's ----------------
  for (int k = 0; k < ON32_EXT_SLOTS; k++) {
    uint32_t have = rd(L.ext_vtable + 4 * k);
    if (k <= 8 || k == 27)
      CHECK(have == L.ext_slots[k]);
    else
      CHECK(have != L.ext_slots[k]);
  }
  uint32_t gi = object(L.gi_vtable, 16);
  gi_obj = gi;
  w32_wr32(L.gi_global, gi);
  uint32_t ext = object(L.ext_vtable, 0xc);

  // the stack words of every replaced slot (each original's `ret N`) and what it answers
  static const struct { int slot, words; uint32_t ret; } calls[] = {
      {9, 0, 0},  {10, 0, 0}, {13, 0, 0}, {14, 0, 0}, {15, 0, 0}, {16, 2, 1}, {17, 0, 0}, {18, 1, 0},
      {19, 1, 0}, {20, 1, 0}, {22, 0, 0}, {23, 0, 0}, {24, 0, 0}, {25, 3, 0}, {26, 0, 0}, {29, 0, 0},
      {30, 0, 0}, {31, 0, 0}, {32, 0, 0}, {33, 0, 0}, {34, 1, 0}, {35, 1, 0}};
  uint32_t v[3] = {0x5000, 0x5004, 0x5008};
  for (size_t k = 0; k < sizeof calls / sizeof calls[0]; k++)
    CHECK((vcall(ext, calls[k].slot, calls[k].words, v) & 0xff) == calls[k].ret);
  CHECK(nevents == 0);

  // RefPtr results through the hidden pointer: NULL
  uint32_t res = w32_galloc(4);
  w32_wr32(res, 0x1234);
  CHECK(vcall(ext, 28, 2, (uint32_t[]){res, 1}) == res && rd(res) == 0);
  w32_wr32(res, 0x1234);
  CHECK(vcall(ext, 36, 1, (uint32_t[]){res}) == res && rd(res) == 0);

  // G2W stats: answered at once
  vcall(ext, 21, 1, v);
  CHECK(nevents == 1 && !strcmp(events[0], "Online/GotG2WStats"));

  // Login: nothing until the next Tick, then one LoginFail per Login, once
  nevents = 0;
  vcall(ext, 11, 0, 0);
  CHECK(nevents == 0);
  vcall(ext, 12, 1, (uint32_t[]){0});
  CHECK(nevents == 0);
  vcall(ext, 11, 0, 0);
  CHECK(nevents == 1 && !strcmp(events[0], "LoginFail/Err"));
  vcall(ext, 11, 0, 0);
  CHECK(nevents == 1);
  for (int k = 0; k < 3; k++) vcall(ext, 12, 1, (uint32_t[]){0});
  vcall(ext, 11, 0, 0);
  CHECK(nevents == 4 && !strcmp(events[3], "LoginFail/Err"));
  // Shutdown drops what is pending; another object has its own count
  uint32_t ext2 = object(L.ext_vtable, 0xc);
  vcall(ext, 12, 1, (uint32_t[]){0});
  vcall(ext2, 12, 1, (uint32_t[]){1});
  vcall(ext, 10, 0, 0);
  vcall(ext, 11, 0, 0);
  CHECK(nevents == 4);
  vcall(ext2, 11, 0, 0);
  CHECK(nevents == 5);
  // no game interface yet: the event is dropped
  w32_wr32(L.gi_global, 0);
  vcall(ext, 12, 1, (uint32_t[]){0});
  vcall(ext, 11, 0, 0);
  CHECK(nevents == 5);
  w32_wr32(L.gi_global, gi);

  // --- GetConfigBool: default true for RendezVous.SkipLogin, the game's answer otherwise --------
  uint32_t skip = efd_heap_string("RendezVous.SkipLogin");
  CHECK(vcall(gi, ON32_GI_SLOT_CONFIG_BOOL, 2, (uint32_t[]){skip, 0}) == 1);
  CHECK(config_this == gi && config_key == skip);
  CHECK(vcall(gi, ON32_GI_SLOT_CONFIG_BOOL, 2, (uint32_t[]){skip, 1}) == 1);
  uint32_t other = efd_inline_string("Net.Other");
  CHECK(vcall(gi, ON32_GI_SLOT_CONFIG_BOOL, 2, (uint32_t[]){other, 0}) == 0);
  CHECK(vcall(gi, ON32_GI_SLOT_CONFIG_BOOL, 2, (uint32_t[]){other, 1}) == 1);
  uint32_t longer = efd_heap_string("RendezVous.SkipLoginX");
  CHECK(vcall(gi, ON32_GI_SLOT_CONFIG_BOOL, 2, (uint32_t[]){longer, 0}) == 0);

  // --- StartSignIn: player 0's sign-in starts with the Uplay dialog done -------------------------
  uint32_t prof = object(L.profile_vtable, 0x100);
  CHECK(vcall(prof, ON32_PROFILE_SLOT_SIGN_IN, 2, (uint32_t[]){0, 0}) == 1);
  CHECK(rd(prof + ON32_PROFILE_SUB) == ON32_PROFILE_SUB_DIALOG_DONE);
  w32_wr32(prof + ON32_PROFILE_SUB, 0);
  CHECK(vcall(prof, ON32_PROFILE_SLOT_SIGN_IN, 2, (uint32_t[]){0, 0}) == 0);  // not started: left alone
  CHECK(rd(prof + ON32_PROFILE_SUB) == 0);
  uint32_t p1 = object(L.profile_vtable, 0x100);
  CHECK(vcall(p1, ON32_PROFILE_SLOT_SIGN_IN, 2, (uint32_t[]){1, 2}) == 1);  // player 1: as the game does
  CHECK(rd(p1 + ON32_PROFILE_SUB) == 0);
  uint32_t p2 = object(L.profile_vtable, 0x100);
  w32_wr32(p2 + ON32_PROFILE_SUB, 2);  // a sub-state already set: kept
  CHECK(vcall(p2, ON32_PROFILE_SLOT_SIGN_IN, 2, (uint32_t[]){0, 1}) == 1);
  CHECK(rd(p2 + ON32_PROFILE_SUB) == 2);

  // --- another build: a slot that does not hold the expected function leaves its group alone ----
  on32_layout M = make_layout();
  w32_wr32(M.gi_vtable + 4 * ON32_GI_SLOT_CONFIG_BOOL, 0x9999);
  M.ext_slots[12] = 0x8888;
  CHECK(on32_install(&M) == ON32_SIGN_IN);
  CHECK(rd(M.gi_vtable + 4 * ON32_GI_SLOT_CONFIG_BOOL) == 0x9999);
  for (int k = 0; k < ON32_EXT_SLOTS; k++) CHECK(rd(M.ext_vtable + 4 * k) == G_ORIG + 16 * (uint32_t)k);

  // the Learn & Play layout: the ext vtable's slots are the RDV extension's thunks, 16 bytes apart
  CHECK(on32_learn_play.ext_slots[12] == 0x0144b560 && on32_learn_play.gi_config_bool == 0x01124120);
  printf("test_online32: ok\n");
  return 0;
}
