// The online seam: the RSConnect factory and the config answers.
#include <string.h>

#include "rc_ov.h"
#include "rs_online.h"

#define EFD_ALLOC 0x1012efc90ull /* (size, hint) */

// --- RSConnectGameInterface as the stub sees it ---------------------------------------------------
// The stub posts its events through slot 58 of the game's RSConnectGameInterface. That object's vtable
// holds guest functions, which native code cannot call: the stub gets a native proxy whose slot 58 runs
// the guest function (and makes the `extra` object the event returns callable from native code).
typedef struct {
  void **vtbl;
  uint64_t real;
} gi_proxy;
static void *gi_vtbl[91];
static gi_proxy gi = {gi_vtbl, 0};

static void gi_event(gi_proxy *self, void *name, void *param, void **extra) {
  uint64_t r = self->real;
  OV_VCALL(r, RS_GAME_INTERFACE_SLOT_EVENT, (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)param,
           (uint64_t)(uintptr_t)extra);
  if (extra && *extra) ov_native_vtable(*extra);
}
static void *hook_game_interface(void *ctx) {
  (void)ctx;
  uint64_t real = RC_LD(uint64_t, RC_A(0x1019033a0 /* RS_ORIG_CONNECT_GAME_INTERFACE */));
  if (!real) return NULL;
  gi.real = real;
  gi_vtbl[RS_GAME_INTERFACE_SLOT_EVENT] = (void *)gi_event;
  return &gi;
}
// Slot 28's empty property object: what 0x100a20570 makes (efd alloc 0x78, ctor 0x1000f9ad0, one reference).
static void *hook_empty_object(void *ctx) {
  (void)ctx;
  uint64_t o = ov_call_guest(EFD_ALLOC, (uint64_t[]){0x78, 0}, 2);
  ov_call_guest(0x1000f9ad0, &o, 1);
  OV_VCALL(o, 0);
  ov_native_vtable((void *)(uintptr_t)o);
  return (void *)(uintptr_t)o;
}

static uint64_t alloc_obj(uint64_t size) { return ov_call_guest(EFD_ALLOC, (uint64_t[]){size, 0}, 2); }

/* 0x100a0c360 the RSConnect factory (disassembly of the original; slots: 1 IncRefCount, 2 DecRefCount,
 * 135/136/137 RSConnectService set DLC / profile / extension service). */
void F_100a0c360(rc_ctx *c) {
  uint64_t g = ov_call_guest(0x1003720e0, NULL, 0);  // the 0x10-byte game interface helper
  if (g) OV_VCALL(g, 1);
  uint64_t steam = alloc_obj(0x148);  // RSSteamService
  ov_call_guest(0x100a06650, &steam, 1);
  if (steam) OV_VCALL(steam, 1);
  uint64_t svc = alloc_obj(0x198);  // RSConnectService_Steam
  ov_call_guest(0x100a04e80, (uint64_t[]){svc, steam}, 2);
  if (svc) OV_VCALL(svc, 1);
  uint64_t dlc = alloc_obj(0x1c0);  // RSDLCService_Steam
  ov_call_guest(0x100a0d560, &dlc, 1);
  if (dlc) OV_VCALL(dlc, 1);
  uint64_t prof = alloc_obj(0xf0);  // RSProfileService_MacOS
  ov_call_guest(0x100a0f9d0, &prof, 1);
  if (prof) OV_VCALL(prof, 1);
  OV_VCALL(svc, 135, dlc);
  OV_VCALL(svc, 136, prof);
  rsg_online_config cfg = {hook_game_interface, hook_empty_object, NULL};
  uint64_t ext = (uint64_t)(uintptr_t)rsg_online_extension_create(&cfg);  // RSConnectExtension_Offline
  {
    // Slots 28 (UplayAccountData), 29 (UplayProfile) and 37 (GetSession)
    // return a RefPtr by value: translated callers pass the result pointer in
    // rdi (x86 sret), the native methods expect it in x8. Register them so
    // rc_icall_native moves it (rc_rt.h).
    extern void rc_native_register_sret(const void *fn);
    const void *const *vt = *(const void *const *const *)(uintptr_t)ext;
    rc_native_register_sret(vt[28]);
    rc_native_register_sret(vt[29]);
    rc_native_register_sret(vt[37]);
  }
  OV_VCALL(ext, 1);
  OV_VCALL(svc, 137, ext);
  uint64_t *gsvc = (uint64_t *)(uintptr_t)RC_A(0x101903398 /* RS_ORIG_CONNECT_SERVICE */);
  uint64_t *ggi = (uint64_t *)(uintptr_t)RC_A(0x1019033a0 /* RS_ORIG_CONNECT_GAME_INTERFACE */);
  if (*gsvc != svc) {
    if (*gsvc) OV_VCALL(*gsvc, 2);
    *gsvc = svc;
    OV_VCALL(svc, 1);
  }
  if (*ggi != g) {
    if (*ggi) OV_VCALL(*ggi, 2);
    *ggi = g;
    if (g) OV_VCALL(g, 1);
  }
  if (ext) OV_VCALL(ext, 2);
  if (prof) OV_VCALL(prof, 2);
  if (dlc) OV_VCALL(dlc, 2);
  if (svc) OV_VCALL(svc, 2);
  if (steam) OV_VCALL(steam, 2);
  if (g) OV_VCALL(g, 2);
  ov_ret_void(c);
}

/* 0x100373cd0 GameInterface slot 68: GetConfigBool(this, key, default). The original asks the efd
 * ConfigManager (ClassID 0x7000005): sm = [0x1000dc450()+8]; svc = sm->GetSystemService(id) (slot 15);
 * svc->QueryInterface(id, &out) (slot 4); out->GetBool(key, default) (slot 21). Same here, except
 * that "RendezVous.SkipLogin" has default true: with no [RendezVous] section (the shipped
 * pkgconfig.ini has none) the sign-in state machine (0x100a13ee0, state 9) never starts a login. */
void F_100373cd0(rc_ctx *c) {
  OV_ARGS(a, c);
  (void)OV_U64(a);  // this
  const char *key = OV_PTR(a, const char);
  uint8_t dflt = (uint8_t)OV_U64(a);
  if (key && strcmp(key, "RendezVous.SkipLogin") == 0) dflt = 1;
  uint8_t res = dflt;
  uint64_t holder = ov_call_guest(0x1000dc450, NULL, 0);
  uint64_t sm = RC_LD(uint64_t, holder + 8);
  uint64_t svc = OV_VCALL(sm, 15, 0x7000005);
  uint64_t out = 0;
  if (svc && (uint8_t)OV_VCALL(svc, 4, 0x7000005, (uint64_t)(uintptr_t)&out) && out)
    res = (uint8_t)OV_VCALL(out, 21, (uint64_t)(uintptr_t)key, dflt);
  ov_ret_u32(c, res);
}

/* --- Enter on the title screen without the Uplay dialogs ---------------------------------
 * 0x100a13950 RSProfileService slot 13, StartSignIn(this, player, pad) -> bool, reached from the title screen's
 * Enter (0x1003ef5f0 / 0x1003eeb40 -> RSConnectService slot 32 0x100a02ae0, a tail call with the same arguments).
 * Ported from its disassembly; the one addition is the last `if`.
 *
 * Why: the profile Tick (slot 11, 0x100a0fa80) takes its no-login branch when player (+0x68) == 0, sub-state
 * (+0xe8) == 0 and sign-in state (+0x30) == 1. With [Net] AutoLogin false (0x101315d70 above) that branch always
 * reads the cloud file `crd`, copies UPName/UPPwd/UPKey to globals and posts RS2UplayLogin (the "Already have a
 * Uplay account?" dialog), sub-state 2, which stops the sign-in machine. Exit on it (code 0x5b/0x5d in
 * UIMenu_UplayLoginDialog 0x1008c0720) opens OnlineSignInRequiredForOnlineFeatures; when that one is cancelled the
 * dialog's update (0x1008c25d0, its state 3) closes itself and calls profile slot 26 (0x100a103e0) through
 * 0x100a05e70: sub-state 4 (plus "save crd if logged in", never true offline). Sub-state 4 lets the machine run.
 * State 1 is written only here, so setting sub-state 4 at this point is that same end state, reached before the
 * Tick can take the branch. Player 1 (+0x68 != 0) never takes it; a nonzero sub-state is left alone. */
#define PS_REGS_PAD 0x08   /* int[2]: pad of each player */
#define PS_PAD_PLAYER 0x10 /* int[4]: player of each pad, -1 = none */
#define PS_ACTIVE 0x28     /* byte[2] */
#define PS_PREV 0x2c
#define PS_STATE 0x30
#define PS_PLAYER 0x68
#define PS_SUB 0xe8
#define PS_SUB_DIALOG_DONE 4
#define PS_SLOT_SET_PAD 17 /* 0x100a13850 (this, player, pad) */
void F_100a13950(rc_ctx *c) {
  OV_ARGS(a, c);
  uint8_t *p = OV_PTR(a, uint8_t);
  int32_t player = OV_I32(a), pad = OV_I32(a);
#define I32AT(off) (*(int32_t *)(p + (off)))
  if (I32AT(PS_PREV) != 0 || I32AT(PS_STATE) != 0 || p[0x7d] || p[0x7f] || (uint32_t)pad > 3 || (uint32_t)player > 1 ||
      p[0x80]) {
    ov_ret_u32(c, 0);
    return;
  }
  int32_t owner = I32AT(PS_PAD_PLAYER + 4 * pad);
  if (I32AT(PS_REGS_PAD + 4 * player) == pad && owner == player) {
    ov_ret_u32(c, 0);
    return;
  }
  if (owner == -1) I32AT(PS_PAD_PLAYER + 4 * pad) = player;
  I32AT(PS_REGS_PAD + 4 * player) = pad;
  OV_VCALL((uint64_t)(uintptr_t)p, PS_SLOT_SET_PAD, (uint64_t)(uint32_t)player, (uint64_t)(uint32_t)pad);
  I32AT(PS_PLAYER) = player;
  I32AT(PS_STATE) = 1;
  p[PS_ACTIVE + player] = 0;
  if (player == 0 && I32AT(PS_SUB) == 0) I32AT(PS_SUB) = PS_SUB_DIALOG_DONE;
#undef I32AT
  ov_ret_u32(c, 1);
}

/* --- the DLC-preview coordinator's job builders ------------------------------------------
 * 0x100a24e70 (worker, std::string &name, job*) queues a preview-file download, 0x100a25170 (worker, type, job*)
 * a metadata/list request, both on the RDV worker (global 0x101904018), which only exists after an RDV login.
 * `job` is the 0x40-byte functor the caller allocated: vtable {0 -, 1 delete, 2 call fn1, 3 call fn2, 4 call fn3
 * (job, bool failed)}, +8 the coordinator or preview item, then three pointer-to-member pairs. fn3 is the worker's
 * completion; with failed = true it releases the in-flight state (flags, the active-download counter at
 * coordinator +0x54, buffers) and asks the tick for the "DLCPreview/DownloadEnded" event. Offline every request
 * therefore ends as a failed transfer, at once, on the calling thread. The builders return true like the original
 * when a job was accepted. */
#define JOB_SLOT_DELETE 1
#define JOB_SLOT_FAIL 4
static uint64_t dlc_job_fail(uint64_t job) {
  if (!job) return 0;
  OV_VCALL(job, JOB_SLOT_FAIL, 1);
  OV_VCALL(job, JOB_SLOT_DELETE);
  return 1;
}
void F_100a24e70(rc_ctx *c) {
  OV_ARGS(a, c);
  (void)OV_U64(a);  // worker (NULL)
  (void)OV_U64(a);  // std::string item name
  ov_ret_u32(c, (uint32_t)dlc_job_fail(OV_U64(a)));
}
void F_100a25170(rc_ctx *c) {
  OV_ARGS(a, c);
  (void)OV_U64(a);  // worker (NULL)
  (void)OV_U64(a);  // request type 0..2
  ov_ret_u32(c, (uint32_t)dlc_job_fail(OV_U64(a)));
}
