// RSConnectExtension_Offline: the arm64 replacement of RSConnectExtension_RDV
// (rs_online.h). Virtuals are declared in slot order,
// so the vtable matches the original's 38 slots (Itanium ABI: the virtual
// destructor takes slots 8 and 9, D1 then D0; a by-value RefPtr result is
// returned through a hidden pointer like the original's slots 28, 29, 37).
//
// Original slot -> RDV manager function, and what the stub does instead:
//   0-9   IBase (destroy, refs at +0x8, QueryInterface, class desc, dtors) as the original
//   10 Init 0x100a1a850, 11 Shutdown 0x100a1abd0         nothing (no session)
//   12 Tick 0x100a1b350                                 posts LoginFail for each Login since the last tick
//   13 Login 0x100a1ad70                                queues the LoginFail; no session, no `crd`
//   14 Logout 0x100a1acb0, 15 0x100a1b260, 16 (empty)   nothing
//   17 Tracking 0x100a22310                             true (telemetry accepted and dropped)
//   18 GAME_START 0x100a22600                           nothing (no sysctlbyname hardware report)
//   19-27 leaderboards, stats, Uplay actions/legal      nothing queued (19, 20, 26, 27: false;
//                                                       21, 23-25 void); 22 posts ("Online",
//                                                       "GotG2WStats") at once, like the original
//   28 Uplay account data 0x100a20570                   the cached empty object (config hook) or NULL
//   29 0x100a20e20, 37 (manager +0x58)                  NULL
//   30-34 connected / logged in / busy                  false
//   35 0x100a23470: false; 36 0x100a235b0 (void)        nothing
#include <stdlib.h>
#include <string.h>

#include "rs_online.h"

extern "C" void rsg_efd_string_init(rsg_efd_string *s, const char *text) {
  size_t n = strlen(text);
  if (n > 15) n = 15;
  memset(s, 0, sizeof *s);
  memcpy(s->u.buf, text, n);
  s->end = s->u.buf + n;
  s->cap = reinterpret_cast<char *>(s) + 0x10;
}

extern "C" const char *rsg_efd_string_data(const rsg_efd_string *s) {
  return s->cap == reinterpret_cast<const char *>(s) + 0x10 ? s->u.buf : s->u.heap;
}

namespace {

// Calls slot `i` of a foreign object (the game's refcounted objects: slot 0
// IncRefCount, slot 1 DecRefCount).
template <typename F>
F slot(void *obj, int i) {
  return reinterpret_cast<F>((*reinterpret_cast<void ***>(obj))[i]);
}

// A smart pointer result: one reference passes to the caller.
struct RefPtr {
  void *p;
  ~RefPtr() {}  // non-trivial: returned in memory, like the original's
};

// efd class-info table, the original's layout ({count; {id, name, ?, offset}...}).
struct ClassEntry {
  uint32_t id;
  const char *name;
  uint64_t reserved;
  uint32_t offset;
};
struct ClassDesc {
  uint32_t count;
  ClassEntry e[2];
};
const ClassDesc k_class_desc = {
    2, {{RS_RDV_EXT_CLASS_ID, "RSConnectExtension_RDV", 0, 0}, {0xffffffffu, "IBase", 0, 0}}};

struct State {
  rsg_online_config cfg;
  int pending_logins;
  void *empty_object;  // slot 28's cached object (one reference)
};

class RSConnectExtensionOffline {
 public:
  explicit RSConnectExtensionOffline(const rsg_online_config &cfg) : refs_(0), st_(new State()) {
    st_->cfg = cfg;
  }

  // --- IBase ---------------------------------------------------------------
  virtual void Destroy() { delete this; }                                   // 0
  virtual void IncRefCount() { ++refs_; }                                   // 1
  virtual void DecRefCount() {                                              // 2
    if (--refs_ == 0) Destroy();
  }
  virtual uint32_t GetRefCount() const { return static_cast<uint32_t>(refs_); }  // 3
  virtual bool QueryInterface(uint32_t id, void **out) {                    // 4
    *out = const_cast<void *>(Find(id));
    return *out != nullptr;
  }
  virtual bool QueryInterfaceConst(uint32_t id, const void **out) const {   // 5
    *out = Find(id);
    return *out != nullptr;
  }
  virtual const ClassDesc *GetClassDesc() const { return &k_class_desc; }  // 6
  virtual uint32_t GetClassID() const { return k_class_desc.e[0].id; }     // 7
  virtual ~RSConnectExtensionOffline() {                                    // 8 D1, 9 D0
    if (st_->empty_object) slot<void (*)(void *)>(st_->empty_object, 1)(st_->empty_object);
    delete st_;
  }

  // --- RSConnectExtension ---------------------------------------------------
  virtual void Init() {}                                                    // 10
  virtual void Shutdown() { st_->pending_logins = 0; }                      // 11
  virtual void Tick() {                                                     // 12
    while (st_->pending_logins > 0) {
      st_->pending_logins--;
      PostEvent("LoginFail", "Err");
    }
  }
  virtual void Login(uint32_t /*player*/) { st_->pending_logins++; }        // 13
  virtual void Logout() {}                                                  // 14
  virtual void CancelPending() {}                                           // 15
  virtual void Slot16() {}                                                  // 16
  virtual bool Track(const void *, const void *) { return true; }           // 17
  virtual void TrackGameStart() {}                                          // 18
  virtual bool LeaderboardQuery(const void *) { return false; }             // 19
  virtual bool LeaderboardPost(const void *) { return false; }              // 20
  virtual void SetStats(const void *) {}                                    // 21
  // 22: the original answers a G2W stats request it cannot send (no session,
  // or no stats asked) at once with the event ("Online", "GotG2WStats").
  virtual void GetG2WStats(const void *) { PostEvent("Online", "GotG2WStats"); }
  virtual void UplayActions() {}                                            // 23
  virtual void UplayRewards() {}                                            // 24
  virtual void UplayLegal() {}                                              // 25
  virtual bool UplayValidate(const void *) { return false; }                // 26
  virtual bool UplayAccountCheck(const void *) { return false; }            // 27
  virtual RefPtr UplayAccountData() {                                       // 28
    if (!st_->empty_object && st_->cfg.new_empty_object)
      st_->empty_object = st_->cfg.new_empty_object(st_->cfg.ctx);
    void *o = st_->empty_object;
    if (o) slot<void (*)(void *)>(o, 0)(o);
    return RefPtr{o};
  }
  virtual RefPtr UplayProfile(bool /*refresh*/) { return RefPtr{nullptr}; }  // 29
  virtual bool IsConnected() const { return false; }                        // 30
  virtual bool IsLoggedIn() const { return false; }                         // 31
  virtual bool IsConnecting() const { return false; }                       // 32
  virtual bool IsBusy() const { return false; }                             // 33
  virtual bool IsLoggingOut() const { return false; }                       // 34
  virtual bool UplayActionComplete(const void *) { return false; }          // 35
  virtual void GetStats(const void *) {}                                    // 36
  virtual RefPtr GetSession() { return RefPtr{nullptr}; }                   // 37

 private:
  const void *Find(uint32_t id) const {
    for (uint32_t i = 0; i < k_class_desc.count; i++)
      if (k_class_desc.e[i].id == id) return reinterpret_cast<const char *>(this) + k_class_desc.e[i].offset;
    return nullptr;
  }

  void PostEvent(const char *name, const char *param) {
    void *gi = st_->cfg.game_interface ? st_->cfg.game_interface(st_->cfg.ctx) : nullptr;
    if (!gi) return;
    rsg_efd_string n, p;
    rsg_efd_string_init(&n, name);
    rsg_efd_string_init(&p, param);
    void *extra = nullptr;
    slot<void (*)(void *, rsg_efd_string *, rsg_efd_string *, void **)>(gi, RS_GAME_INTERFACE_SLOT_EVENT)(gi, &n, &p,
                                                                                                         &extra);
    if (extra) slot<void (*)(void *)>(extra, 1)(extra);
  }

  int32_t refs_;  // +0x8, as the original
  State *st_;     // +0x10, where the original keeps its RDV manager
};

static_assert(sizeof(RSConnectExtensionOffline) == 0x18, "the original's object size");

}  // namespace

extern "C" void *rsg_online_extension_create(const rsg_online_config *cfg) {
  rsg_online_config c = {};
  if (cfg) c = *cfg;
  return new RSConnectExtensionOffline(c);
}
