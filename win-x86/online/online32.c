// The offline online layer of the Windows build (online32.h): RSConnectExtension_RDV's slots as
// the Mac build's RSConnectExtension_Offline answers them, and the two wrappers that keep the
// title screen's Enter away from the Uplay sign-in.
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "online32.h"
#include "win32_guestobj.h"

// --- the Learn & Play image ------------------------------------------------------------------
const on32_layout on32_learn_play = {
    .ext_vtable = 0x01d5d934,
    .ext_slots = {0x0147a8b0, 0x01548820, 0x014474c0, 0x012f6490, 0x0144b2a0, 0x0144b2a0, 0x0144b290,
                  0x00f81300, 0x0144b470, 0x0144b500, 0x0144b510, 0x0144b520, 0x0144b560, 0x0144b570,
                  0x0144b580, 0x012ce550, 0x0144b530, 0x0144b550, 0x0144b590, 0x0144b5b0, 0x0144b5d0,
                  0x0144b5f0, 0x0144b610, 0x0144b620, 0x0144b630, 0x0144b750, 0x0144b770, 0x0144b640,
                  0x0144b670, 0x0144b6a0, 0x0144b6c0, 0x0144b6e0, 0x0144b710, 0x0144b730, 0x0144b7c0,
                  0x0144b7e0, 0x0144b800},
    .gi_vtable = 0x01d2d590,
    .gi_global = 0x01ef5e94,
    .gi_config_bool = 0x01124120,
    .profile_vtable = 0x01d5d894,
    .profile_sign_in = 0x0143eae0,
};

static const on32_layout *lay;  // the installed layout (the host functions read it)

// --- events to the game's RSConnectGameInterface ---------------------------------------------
// The efd string the event method takes by reference (0x18 bytes): 16 bytes inline text or a heap
// pointer at +0, the end of the text at +0x10, the end of the buffer at +0x14 (== this + 0x10
// while the text is inline). Inline strings only (<= 15 chars), all the layer needs.
#define EFD_STR_SIZE 0x18
static uint32_t efd_str_new(const char *text) {
  uint32_t s = w32_galloc(EFD_STR_SIZE);
  if (!s) return 0;
  size_t n = strlen(text);
  if (n > 15) n = 15;
  memcpy(RC32_H(s), text, n);  // w32_galloc zero-fills: the text is NUL-terminated
  w32_wr32(s + 0x10, s + (uint32_t)n);
  w32_wr32(s + 0x14, s + 0x10);
  return s;
}
// The text of an efd string the game passed (inline or on its heap), NUL-terminated.
static const char *efd_str_text(uint32_t s) {
  if (!s) return 0;
  uint32_t data = w32_rd32(s + 0x14) == s + 0x10 ? s : w32_rd32(s);
  return data ? (const char *)RC32_H(data) : 0;
}

// RSConnectGameInterface slot 57 (name, param, extra = NULL), what the RDV manager's tick does for
// each finished request.
static void post_event(rc_ctx *c, const char *name, const char *param) {
  uint32_t gi = w32_rd32(lay->gi_global);
  if (!gi) return;
  uint32_t n = efd_str_new(name), p = efd_str_new(param);
  if (n && p)
    w32_guest_call(c, w32_rd32(w32_rd32(gi) + 4 * ON32_GI_SLOT_EVENT), W32_THISCALL, gi, (uint32_t[]){n, p, 0}, 3, 0);
  w32_gfree(n, EFD_STR_SIZE);
  w32_gfree(p, EFD_STR_SIZE);
}

// --- RSConnectExtension_RDV as the offline extension -------------------------------------------
// The one state the extension keeps: Logins not yet answered, per object (the game makes one).
#define MAX_EXT 8
static struct {
  uint32_t obj;
  int pending;
} ext_state[MAX_EXT];
static pthread_mutex_t ext_mu = PTHREAD_MUTEX_INITIALIZER;

static int *pending_of(uint32_t obj) {
  for (int k = 0; k < MAX_EXT; k++)
    if (ext_state[k].obj == obj) return &ext_state[k].pending;
  for (int k = 0; k < MAX_EXT; k++)
    if (!ext_state[k].obj) {
      ext_state[k].obj = obj;
      return &ext_state[k].pending;
    }
  return 0;
}

static uint64_t x_nothing(rc_ctx *c, uint32_t self, const uint32_t *a) { return 0; }
static uint64_t x_true(rc_ctx *c, uint32_t self, const uint32_t *a) { return 1; }
// A RefPtr result through the hidden pointer: NULL.
static uint64_t x_null_ref(rc_ctx *c, uint32_t self, const uint32_t *a) {
  w32_wr32(a[0], 0);
  return a[0];
}
static uint64_t x_shutdown(rc_ctx *c, uint32_t self, const uint32_t *a) {
  pthread_mutex_lock(&ext_mu);
  int *p = pending_of(self);
  if (p) *p = 0;
  pthread_mutex_unlock(&ext_mu);
  return 0;
}
static uint64_t x_login(rc_ctx *c, uint32_t self, const uint32_t *a) {
  pthread_mutex_lock(&ext_mu);
  int *p = pending_of(self);
  if (p) ++*p;
  pthread_mutex_unlock(&ext_mu);
  return 0;
}
// Tick: a login job fails in the original's tick; here each Login since the last Tick does.
static uint64_t x_tick(rc_ctx *c, uint32_t self, const uint32_t *a) {
  for (;;) {
    pthread_mutex_lock(&ext_mu);
    int *p = pending_of(self), go = p && *p > 0;
    if (go) --*p;
    pthread_mutex_unlock(&ext_mu);
    if (!go) return 0;
    post_event(c, "LoginFail", "Err");
  }
}
// The original answers a G2W stats request it cannot send at once.
static uint64_t x_g2w_stats(rc_ctx *c, uint32_t self, const uint32_t *a) {
  post_event(c, "Online", "GotG2WStats");
  return 0;
}

// Windows slot -> (stack words, host function); words from each original's `ret N`.
// Mac slot = Windows slot + 1 from slot 9 on (the Mac names, shims/game/src/online_stub.cpp).
typedef struct {
  int slot;
  const char *name;
  int words;
  w32_hostfn fn;
} ext_slot;
static const ext_slot ext_slots[] = {
    {9, "RSConnectExtension::Init", 0, x_nothing},
    {10, "RSConnectExtension::Shutdown", 0, x_shutdown},
    {11, "RSConnectExtension::Tick", 0, x_tick},
    {12, "RSConnectExtension::Login", 1, x_login},
    {13, "RSConnectExtension::Logout", 0, x_nothing},
    {14, "RSConnectExtension::CancelPending", 0, x_nothing},
    {15, "RSConnectExtension::Slot16", 0, x_nothing},
    {16, "RSConnectExtension::Track", 2, x_true},  // telemetry accepted and dropped
    {17, "RSConnectExtension::TrackGameStart", 0, x_nothing},
    {18, "RSConnectExtension::LeaderboardQuery", 1, x_nothing},
    {19, "RSConnectExtension::LeaderboardPost", 1, x_nothing},
    {20, "RSConnectExtension::SetStats", 1, x_nothing},
    {21, "RSConnectExtension::GetG2WStats", 1, x_g2w_stats},
    {22, "RSConnectExtension::UplayActions", 0, x_nothing},
    {23, "RSConnectExtension::UplayRewards", 0, x_nothing},
    {24, "RSConnectExtension::UplayLegal", 0, x_nothing},
    {25, "RSConnectExtension::UplayValidate", 3, x_nothing},
    {26, "RSConnectExtension::UplayAccountCheck", 0, x_nothing},
    // 27 UplayAccountData: the game's own (the cached empty property object)
    {28, "RSConnectExtension::UplayProfile", 2, x_null_ref},  // (RefPtr *result, bool refresh)
    {29, "RSConnectExtension::IsConnected", 0, x_nothing},
    {30, "RSConnectExtension::IsLoggedIn", 0, x_nothing},
    {31, "RSConnectExtension::IsConnecting", 0, x_nothing},
    {32, "RSConnectExtension::IsBusy", 0, x_nothing},
    {33, "RSConnectExtension::IsLoggingOut", 0, x_nothing},
    {34, "RSConnectExtension::UplayActionComplete", 1, x_nothing},
    {35, "RSConnectExtension::GetStats", 1, x_nothing},
    {36, "RSConnectExtension::GetSession", 1, x_null_ref},  // (RefPtr *result)
};

// --- the two wrappers --------------------------------------------------------------------------
// GameInterface slot 67, GetConfigBool(this, const efd string &key, bool default): the game's,
// with default true for "RendezVous.SkipLogin". With no [RendezVous] section in the config (the
// shipped one has none) the sign-in state machine then never starts a login.
static uint64_t w_config_bool(rc_ctx *c, uint32_t self, const uint32_t *a) {
  uint32_t dflt = a[1];
  const char *key = efd_str_text(a[0]);
  if (key && strncmp(key, "RendezVous.SkipLogin", 21) == 0) dflt = (dflt & ~0xffu) | 1;
  return w32_guest_call(c, lay->gi_config_bool, W32_THISCALL, self, (uint32_t[]){a[0], dflt}, 2, 0);
}

// RSProfileService slot 12, StartSignIn(this, player, pad) -> bool: the game's, then sub-state 4
// for player 0 when it started a sign-in (Mac signin_start_no_uplay: the profile Tick's sub-state 0
// branch, which reads the cloud file `crd` and posts RS2UplayLogin or, with [Net] AutoLogin, logs
// in, is the state the Uplay dialog's close leaves, slot 25, reached before the Tick can take it).
static uint64_t w_sign_in(rc_ctx *c, uint32_t self, const uint32_t *a) {
  uint32_t r = w32_guest_call(c, lay->profile_sign_in, W32_THISCALL, self, (uint32_t[]){a[0], a[1]}, 2, 0);
  if ((r & 0xff) && a[0] == 0 && w32_rd32(self + ON32_PROFILE_SUB) == 0)
    w32_wr32(self + ON32_PROFILE_SUB, ON32_PROFILE_SUB_DIALOG_DONE);
  return r;
}

// --- install -------------------------------------------------------------------------------------
static int slot_is(uint32_t vtable, int slot, uint32_t want, const char *what) {
  uint32_t have = w32_rd32(vtable + 4 * (uint32_t)slot);
  if (have == want) return 1;
  fprintf(stderr, "lp: online: %s slot %d holds 0x%x, not 0x%x: left alone\n", what, slot, have, want);
  return 0;
}

int on32_install(const on32_layout *L) {
  static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
  static const on32_layout *done;
  int got = 0;
  pthread_mutex_lock(&mu);
  if (done == L) {
    pthread_mutex_unlock(&mu);
    return 0;
  }
  lay = L;
  int ok = 1;
  for (int k = 0; k < ON32_EXT_SLOTS; k++) ok &= slot_is(L->ext_vtable, k, L->ext_slots[k], "RSConnectExtension_RDV");
  if (ok) {
    for (size_t k = 0; k < sizeof ext_slots / sizeof ext_slots[0]; k++) {
      const ext_slot *s = &ext_slots[k];
      uint32_t cell = w32_hostfn_new(s->name, W32_THISCALL, s->words, s->fn, 0);
      if (cell) w32_wr32(L->ext_vtable + 4 * (uint32_t)s->slot, cell);
    }
    got |= ON32_EXT;
  }
  if (slot_is(L->gi_vtable, ON32_GI_SLOT_CONFIG_BOOL, L->gi_config_bool, "RSConnectGameInterface")) {
    uint32_t cell = w32_hostfn_new("RSConnectGameInterface::GetConfigBool", W32_THISCALL, 2, w_config_bool, 0);
    if (cell) {
      w32_wr32(L->gi_vtable + 4 * ON32_GI_SLOT_CONFIG_BOOL, cell);
      got |= ON32_CONFIG;
    }
  }
  if (slot_is(L->profile_vtable, ON32_PROFILE_SLOT_SIGN_IN, L->profile_sign_in, "RSProfileService_Win32")) {
    uint32_t cell = w32_hostfn_new("RSProfileService::StartSignIn", W32_THISCALL, 2, w_sign_in, 0);
    if (cell) {
      w32_wr32(L->profile_vtable + 4 * ON32_PROFILE_SLOT_SIGN_IN, cell);
      got |= ON32_SIGN_IN;
    }
  }
  done = L;
  pthread_mutex_unlock(&mu);
  fprintf(stderr, "lp: online: offline RSConnect layer%s%s%s\n", got & ON32_EXT ? " +extension" : "",
          got & ON32_CONFIG ? " +SkipLogin" : "", got & ON32_SIGN_IN ? " +sign-in" : "");
  return got;
}
