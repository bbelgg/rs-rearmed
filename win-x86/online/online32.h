// The offline online layer of the Windows build: the Learn & Play counterpart of the Mac build's
// `online` override module (shims/game/src/online_stub.cpp, tools/ws7/recomp/overrides/src/online_glue.c).
//
// The Mac build replaces the RSConnect factory and builds RSConnectExtension_Offline, a native
// object. The Windows build keeps the game's own objects and patches their vtables at run time,
// before the game starts: each patched slot becomes a thunk cell (win32_guestobj.h) whose host
// function answers the call. Nothing is re-translated, every caller of a slot (all of them call
// through the vtable) reaches the host function, and the original function stays callable for
// the wrappers that add to it.
//
//   RSConnectExtension_RDV (37 slots; MSVC: one scalar deleting destructor, slot 8, where the
//     Itanium vtable has two, so Mac slot m >= 10 is Windows slot m - 1):
//     0-8 IBase and the destructor stay the game's (the reference count at +4, QueryInterface,
//     the class info "RSConnectExtension_RDV"); 27 (Uplay account data) stays the game's (it
//     makes and caches the empty property object in the sessionless manager, no network);
//     every other slot (9-26, 28-36) is the offline extension: not connected, not logged in,
//     nothing queued, Login answered with the game's own ("LoginFail", "Err") event from the
//     next Tick, the G2W stats request with ("Online", "GotG2WStats") at once.
//   RSConnectGameInterface_Impl slot 67, GetConfigBool(key, default): the game's function with
//     default true for "RendezVous.SkipLogin" (the Mac efd_get_config_bool), so the sign-in state
//     machine never starts a login.
//   RSProfileService_Win32 slot 12, StartSignIn(player, pad): the game's function, then sub-state
//     4 for player 0 (the Mac signin_start_no_uplay): Enter on the title screen goes to the
//     profile list without the Uplay dialogs.
//
// The addresses are the Learn & Play image's (runtime addresses, image base 0xf80000; the evidence
// for each is in win-x86/online/names.tsv). The install checks every slot it patches still holds
// the function it expects and leaves a group alone when one does not (another build of the game).
#ifndef ONLINE32_H
#define ONLINE32_H

#include <stdint.h>

#include "rc32.h"

#define ON32_EXT_SLOTS 37
#define ON32_GI_SLOT_EVENT 57         // RSConnectGameInterface: (name &, param &, extra by value), ret 0xc
#define ON32_GI_SLOT_CONFIG_BOOL 67   // RSConnectGameInterface: GetConfigBool(key &, default), ret 8
#define ON32_PROFILE_SLOT_SIGN_IN 12  // RSProfileService: StartSignIn(player, pad), ret 8
#define ON32_PROFILE_SUB 0xcc         // RSProfileService sub-state (int); 4 = the Uplay dialog is done
#define ON32_PROFILE_SUB_DIALOG_DONE 4

typedef struct {
  uint32_t ext_vtable;                     // RSConnectExtension_RDV's vtable
  uint32_t ext_slots[ON32_EXT_SLOTS];      // what each of its slots holds before the patch
  uint32_t gi_vtable;                      // RSConnectGameInterface_Impl's vtable
  uint32_t gi_global;                      // RSConnectGameInterface * (the events go to it)
  uint32_t gi_config_bool;                 // its slot ON32_GI_SLOT_CONFIG_BOOL
  uint32_t profile_vtable;                 // RSProfileService_Win32's vtable
  uint32_t profile_sign_in;                // its slot ON32_PROFILE_SLOT_SIGN_IN
} on32_layout;

// The Learn & Play image's layout.
extern const on32_layout on32_learn_play;

// What on32_install patched (bits).
enum { ON32_EXT = 1, ON32_CONFIG = 2, ON32_SIGN_IN = 4 };

// Patch the vtables of the mapped image. Call after the image is mapped and bound, before the game
// runs. Returns the groups patched; logs each group it leaves alone. Once per layout.
int on32_install(const on32_layout *L);

#endif
