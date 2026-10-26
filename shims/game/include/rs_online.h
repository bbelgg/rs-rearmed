// The offline replacement of the game's Rendez-Vous/UbiServices seam.
//
// RSConnectExtension_Offline stands in for RSConnectExtension_RDV (vtable
// 0x101822c98, 38 slots, ClassID 0x1000719, ctor 0x100a04590, sizeof 0x18):
// the object RSConnectService slot 130 returns and slot 137 stores. It keeps
// the original's slot layout, so original and rewritten callers can use it,
// and answers every request the way the original does without a Rendez-Vous
// session: not connected, not logged in, nothing queued. It never touches the
// network, never reads the `crd` credentials file, and links nothing from
// UbiServices, Quazal, libcurl or OpenSSL (test game.offline.no_network).
//
// Login (slot 13) is the one request the game waits on: the stub answers it
// with the game's own "servers unavailable" path, the LoginFail event the RDV
// manager's tick (0x100a1b350) posts for a failed login job:
//   RSConnectGameInterface slot 58 (0x1d0) ("LoginFail", "Err", NULL)
// It is posted from the next Tick (slot 12), like the original, which only
// learns the result of a login job in its tick.
#ifndef RS_ONLINE_H
#define RS_ONLINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS_ORIG_RDV_EXT_VTABLE 0x101822c98
#define RS_ORIG_RDV_EXT_CTOR 0x100a04590 /* (this): sizeof 0x18, manager (0x80) at +0x10 */
#define RS_ORIG_RDV_EXT_CLASS_DESC 0x101902b88
#define RS_ORIG_RDV_MGR_TICK 0x100a1b350
#define RS_ORIG_CONNECT_GAME_INTERFACE 0x1019033a0 /* RSConnectGameInterface * */
#define RS_ORIG_CONNECT_SERVICE 0x101903398        /* RSConnectService * */
#define RS_RDV_EXT_CLASS_ID 0x1000719u
#define RS_GAME_INTERFACE_SLOT_EVENT 58 /* (this, const efd string &name, &param, Ptr &extra) */

// The efd small-string layout the event call takes (32 bytes, 15 chars inline).
typedef struct {
  union {
    char buf[16];
    char *heap;
  } u;
  char *end;  // data + length
  char *cap;  // == (char *)this + 0x10 while the text is inline
} rsg_efd_string;
// Inline strings only (<= 15 chars), all the stub needs.
void rsg_efd_string_init(rsg_efd_string *s, const char *text);
const char *rsg_efd_string_data(const rsg_efd_string *s);

typedef struct {
  // Returns the RSConnectGameInterface (0x100a05b80 in the original reads
  // the global 0x1019033a0). Called when an event is posted.
  void *(*game_interface)(void *ctx);
  // Slot 28 (Uplay account data) returns an empty property object even
  // without a session: the original makes one (efd alloc 0x78, ctor
  // 0x1000f9ad0) and caches it. This hook makes it, with one reference for
  // the stub; NULL makes slot 28 return a NULL pointer instead.
  void *(*new_empty_object)(void *ctx);
  void *ctx;
} rsg_online_config;

// Creates the extension with reference count 0, like the original's
// constructor (the owner takes a reference with IncRefCount, slot 1; the last
// DecRefCount, slot 2, destroys it through slot 0).
void *rsg_online_extension_create(const rsg_online_config *cfg);

#ifdef __cplusplus
}
#endif

#endif
