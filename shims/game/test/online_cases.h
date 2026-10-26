// Tests of the offline online-layer stub (include/rs_online.h) at its
// boundary, shared by harness/difftest.c and test/native_test.c.
//
// online_ext_transcript() drives an RSConnectExtension through its vtable,
// slot by slot, and writes what it sees into the efd transcript: reference
// counts, QueryInterface and class info, the status queries, every request
// the game can make without a session, the smart-pointer results, and every
// event posted to a fake RSConnectGameInterface. The x86_64 test runs it on
// the original RSConnectExtension_RDV (built by its constructor 0x100a04590,
// so its RDV manager has no session) and on the stub, and the two
// transcripts must be equal; the arm64 test checks the stub's against the
// original's CRC.
//
// The one deliberate difference is how a failed login comes about: the
// original needs a finished login job in its manager's queue (the test
// injects one: type 4, state 3 = failed, a generic error), the stub needs
// Login (slot 13). Both must then post the same event from Tick.
#ifndef RS_GAME_ONLINE_CASES_H
#define RS_GAME_ONLINE_CASES_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "efd_fakes.h"
#include "rs_online.h"

// --- fake RSConnectGameInterface (91 slots; only slot 58 is expected) ----------
#define GI_SLOTS 91
static void *gi_vtbl[GI_SLOTS];
static struct {
  void **vtbl;
} fake_gi = {gi_vtbl};

static void gi_event(void *self, const rsg_efd_string *name, const rsg_efd_string *param, void **extra) {
  (void)self;
  const char *n = rsg_efd_string_data(name), *p = rsg_efd_string_data(param);
  efd_txf("event(%.*s, %.*s, extra %s)\n", (int)(name->end - n), n, (int)(param->end - p), p,
          *extra ? "set" : "null");
}
static void *gi_unexpected(void *self) {
  (void)self;
  efd_txf("game interface: unexpected slot\n");
  return NULL;
}
static void online_fakes_init(void) {
  for (int i = 0; i < GI_SLOTS; i++) gi_vtbl[i] = (void *)gi_unexpected;
  gi_vtbl[RS_GAME_INTERFACE_SLOT_EVENT] = (void *)gi_event;
}
static void *online_game_interface(void *ctx) {
  (void)ctx;
  return &fake_gi;
}

// --- calling an extension through its vtable ------------------------------------
// A by-value smart pointer comes back through a hidden pointer; a C struct
// over 16 bytes is returned the same way on x86_64 and arm64 (x8).
typedef struct {
  void *p;
  void *pad[2];
} online_sret;
#define EXT_FN(obj, slot, type) ((type)((*(void ***)(obj))[slot]))
typedef bool (*ext_bool0)(void *);
typedef bool (*ext_bool1)(void *, const void *);
typedef bool (*ext_bool2)(void *, const void *, const void *);
typedef void (*ext_void0)(void *);
typedef void (*ext_void1)(void *, const void *);
typedef online_sret (*ext_sret0)(void *);
typedef online_sret (*ext_sret_b)(void *, bool);

static void obj_release(void *o) { EXT_FN(o, 1, void (*)(void *))(o); }

// Runs the sequence. login_fail(ext) must make the extension's next Tick
// report a failed login (see above).
static void online_ext_transcript(void *ext, void (*login_fail)(void *)) {
  efd_tx_reset();
  efd_txf("refs %u\n", EXT_FN(ext, 3, uint32_t (*)(void *))(ext));
  EXT_FN(ext, 1, ext_void0)(ext);
  efd_txf("after IncRefCount: refs %u\n", EXT_FN(ext, 3, uint32_t (*)(void *))(ext));
  static const uint32_t ids[] = {RS_RDV_EXT_CLASS_ID, 0xffffffffu, 0x7000001u, 0x1000702u, 0u};
  for (size_t i = 0; i < sizeof ids / sizeof *ids; i++) {
    void *o1 = (void *)1, *o2 = (void *)1;
    bool r1 = EXT_FN(ext, 4, bool (*)(void *, uint32_t, void **))(ext, ids[i], &o1);
    bool r2 = EXT_FN(ext, 5, bool (*)(void *, uint32_t, void **))(ext, ids[i], &o2);
    efd_txf("QueryInterface(%#x): %d %s / %d %s\n", ids[i], r1, o1 == ext ? "this" : o1 ? "other" : "null", r2,
            o2 == ext ? "this" : o2 ? "other" : "null");
  }
  const uint32_t *desc = EXT_FN(ext, 6, const uint32_t *(*)(void *))(ext);
  efd_txf("class desc: %u entries:", desc[0]);
  for (uint32_t i = 0; i < desc[0] && i < 8; i++) efd_txf(" %#x", desc[2 + 8 * i]);
  efd_txf("; GetClassID %#x\n", EXT_FN(ext, 7, uint32_t (*)(void *))(ext));

  EXT_FN(ext, 12, ext_void0)(ext);
  efd_txf("Tick: no events\n");
  static const char *const flag_names[] = {"connected", "logged in", "connecting", "busy", "logging out"};
  for (int s = 30; s <= 34; s++) efd_txf("slot %d (%s): %d\n", s, flag_names[s - 30], EXT_FN(ext, s, ext_bool0)(ext));

  // Requests the game can make without a session (the rest, 19, 20, 26, 27
  // and 18, queue jobs on the session in the original and are not reachable
  // offline: the stub answers them in online_stub_extra()).
  uint64_t empty_vec[3] = {0, 0, 0};  // an empty std::vector argument
  // 21-25 and 36 return nothing (the original leaves rax as it was).
  EXT_FN(ext, 21, ext_void1)(ext, empty_vec);
  efd_txf("slot 21 (stat sets): done\n");
  EXT_FN(ext, 22, ext_void1)(ext, empty_vec);
  efd_txf("slot 22 (G2W stats): done\n");
  for (int s = 23; s <= 25; s++) {
    EXT_FN(ext, s, ext_void0)(ext);
    efd_txf("slot %d (Uplay): done\n", s);
  }
  efd_txf("slot 35: %d\n", EXT_FN(ext, 35, ext_bool1)(ext, empty_vec));
  EXT_FN(ext, 36, ext_void1)(ext, empty_vec);
  efd_txf("slot 36: done\n");
  efd_txf("slot 17 (tracking): %d\n", EXT_FN(ext, 17, ext_bool2)(ext, empty_vec, empty_vec));

  online_sret a = EXT_FN(ext, 28, ext_sret0)(ext), b = EXT_FN(ext, 28, ext_sret0)(ext);
  efd_txf("slot 28 (account data): %s, again the same object: %d\n", a.p ? "object" : "null", a.p == b.p);
  if (a.p) obj_release(a.p);
  if (b.p) obj_release(b.p);
  online_sret c = EXT_FN(ext, 29, ext_sret_b)(ext, false), d = EXT_FN(ext, 29, ext_sret_b)(ext, true);
  online_sret e = EXT_FN(ext, 37, ext_sret0)(ext);
  efd_txf("slot 29: %s / %s, slot 37: %s\n", c.p ? "object" : "null", d.p ? "object" : "null", e.p ? "object" : "null");

  EXT_FN(ext, 14, ext_void0)(ext);
  EXT_FN(ext, 15, ext_void0)(ext);
  EXT_FN(ext, 16, ext_void0)(ext);
  EXT_FN(ext, 12, ext_void0)(ext);
  efd_txf("Logout, 15, 16, Tick: done\n");
  for (int s = 30; s <= 34; s++) efd_txf("slot %d: %d\n", s, EXT_FN(ext, s, ext_bool0)(ext));

  login_fail(ext);
  efd_txf("login failed; Tick:\n");
  EXT_FN(ext, 12, ext_void0)(ext);
  EXT_FN(ext, 12, ext_void0)(ext);
  efd_txf("second Tick: done\n");
  for (int s = 30; s <= 34; s++) efd_txf("slot %d: %d\n", s, EXT_FN(ext, s, ext_bool0)(ext));
  efd_txf("refs %u\n", EXT_FN(ext, 3, uint32_t (*)(void *))(ext));
}

// The stub's own login path.
static void online_stub_login(void *ext) { EXT_FN(ext, 13, void (*)(void *, uint32_t))(ext, 0); }

// What only the stub can be asked offline (the original would queue on a
// NULL session): the answers the spec requires.
static void online_stub_extra(void *ext) {
  efd_tx_reset();
  uint64_t v[3] = {0, 0, 0};
  EXT_FN(ext, 10, ext_void0)(ext);
  efd_txf("Init: done\n");
  for (int s = 19; s <= 20; s++) efd_txf("slot %d: %d\n", s, EXT_FN(ext, s, ext_bool1)(ext, v));
  for (int s = 26; s <= 27; s++) efd_txf("slot %d: %d\n", s, EXT_FN(ext, s, ext_bool1)(ext, v));
  EXT_FN(ext, 18, ext_void0)(ext);
  efd_txf("slot 18: done\n");
  for (int k = 0; k < 3; k++) online_stub_login(ext);
  EXT_FN(ext, 12, ext_void0)(ext);
  efd_txf("3 logins, one Tick: done\n");
  EXT_FN(ext, 11, ext_void0)(ext);
  efd_txf("Shutdown: done\n");
}

#endif
