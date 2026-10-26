// Fake efd objects for the efd differential tests, and the transcript they
// write. The fakes are plain C structs with a hand-built vtable (Itanium
// layout: vptr at +0, slot i at vptr[i]), so the original x86_64 code calls
// them like the real objects and so do the C++ rewrites on either
// architecture. Everything a fake is asked is appended to the transcript; a
// test compares the original's transcript with the rewrite's.
//
//   fake_logger           efd::ILogger: slot 11 Log, slot 23 IsLogging (always on)
//   fake_service_manager  efd::ServiceManager: slot 15 GetSystemService(ClassID)
//   fake_message_service  efd::MessageService: slot 4 QueryInterface, 25 GetNetID,
//                         27 Subscribe, 28 Unsubscribe
// Calls to any other slot are recorded as "unexpected".
#ifndef RS_GAME_EFD_FAKES_H
#define RS_GAME_EFD_FAKES_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- transcript ---------------------------------------------------------------
static char *efd_tx;
static size_t efd_tx_len, efd_tx_cap;
static int efd_tx_lines;

static void efd_tx_reset(void) {
  efd_tx_len = 0;
  efd_tx_lines = 0;
  if (efd_tx) efd_tx[0] = 0;
}

__attribute__((format(printf, 1, 2))) static void efd_txf(const char *fmt, ...) {
  va_list ap;
  for (;;) {
    va_start(ap, fmt);
    size_t room = efd_tx_cap - efd_tx_len;
    int n = vsnprintf(efd_tx ? efd_tx + efd_tx_len : NULL, efd_tx ? room : 0, fmt, ap);
    va_end(ap);
    if (efd_tx && (size_t)n < room) {
      for (int i = 0; i < n; i++) efd_tx_lines += efd_tx[efd_tx_len + i] == '\n';
      efd_tx_len += (size_t)n;
      break;
    }
    efd_tx_cap = efd_tx_cap * 2 + (size_t)n + 4096;
    efd_tx = (char *)realloc(efd_tx, efd_tx_cap);
  }
}

static const char *efd_basename(const char *path) {
  const char *b = path ? strrchr(path, '/') : NULL;
  return b ? b + 1 : path ? path : "(null)";
}

// The rewrites' log hook (rsg_efd_set_log) and the fake logger write the same line.
static void efd_log_line(int module, int level, const char *file, int line, const char *msg) {
  efd_txf("log m%d l%d %s:%d %s\n", module, level, efd_basename(file), line, msg ? msg : "(null)");
}

// --- fakes ---------------------------------------------------------------------
#define EFD_FAKE_SLOTS 40
typedef struct {
  void **vtbl;
  const char *name;
} efd_fake_hdr;

#define EFD_UNEXPECTED(n)                                                            \
  static void *efd_unexpected_##n(void *self) {                                     \
    efd_txf("unexpected slot %d of %s\n", n, ((efd_fake_hdr *)self)->name);          \
    return NULL;                                                                     \
  }
EFD_UNEXPECTED(0) EFD_UNEXPECTED(1) EFD_UNEXPECTED(2) EFD_UNEXPECTED(3) EFD_UNEXPECTED(4)
EFD_UNEXPECTED(5) EFD_UNEXPECTED(6) EFD_UNEXPECTED(7) EFD_UNEXPECTED(8) EFD_UNEXPECTED(9)
EFD_UNEXPECTED(10) EFD_UNEXPECTED(11) EFD_UNEXPECTED(12) EFD_UNEXPECTED(13) EFD_UNEXPECTED(14)
EFD_UNEXPECTED(15) EFD_UNEXPECTED(16) EFD_UNEXPECTED(17) EFD_UNEXPECTED(18) EFD_UNEXPECTED(19)
EFD_UNEXPECTED(20) EFD_UNEXPECTED(21) EFD_UNEXPECTED(22) EFD_UNEXPECTED(23) EFD_UNEXPECTED(24)
EFD_UNEXPECTED(25) EFD_UNEXPECTED(26) EFD_UNEXPECTED(27) EFD_UNEXPECTED(28) EFD_UNEXPECTED(29)
EFD_UNEXPECTED(30) EFD_UNEXPECTED(31) EFD_UNEXPECTED(32) EFD_UNEXPECTED(33) EFD_UNEXPECTED(34)
EFD_UNEXPECTED(35) EFD_UNEXPECTED(36) EFD_UNEXPECTED(37) EFD_UNEXPECTED(38) EFD_UNEXPECTED(39)

static void efd_fill_unexpected(void **v) {
  void *(*const f[EFD_FAKE_SLOTS])(void *) = {
      efd_unexpected_0,  efd_unexpected_1,  efd_unexpected_2,  efd_unexpected_3,  efd_unexpected_4,
      efd_unexpected_5,  efd_unexpected_6,  efd_unexpected_7,  efd_unexpected_8,  efd_unexpected_9,
      efd_unexpected_10, efd_unexpected_11, efd_unexpected_12, efd_unexpected_13, efd_unexpected_14,
      efd_unexpected_15, efd_unexpected_16, efd_unexpected_17, efd_unexpected_18, efd_unexpected_19,
      efd_unexpected_20, efd_unexpected_21, efd_unexpected_22, efd_unexpected_23, efd_unexpected_24,
      efd_unexpected_25, efd_unexpected_26, efd_unexpected_27, efd_unexpected_28, efd_unexpected_29,
      efd_unexpected_30, efd_unexpected_31, efd_unexpected_32, efd_unexpected_33, efd_unexpected_34,
      efd_unexpected_35, efd_unexpected_36, efd_unexpected_37, efd_unexpected_38, efd_unexpected_39};
  for (int i = 0; i < EFD_FAKE_SLOTS; i++) v[i] = (void *)f[i];
}

// ILogger
static void efd_fake_log(void *self, int module, int level, const char *file, int line, const char *msg,
                         void *site) {
  (void)self;
  (void)site;
  efd_log_line(module, level, file, line, msg);
}
static bool efd_fake_is_logging(void *self, int module, int level) {
  (void)self;
  (void)module;
  (void)level;
  return true;
}

static void *efd_logger_vtbl[EFD_FAKE_SLOTS];
static efd_fake_hdr efd_logger = {efd_logger_vtbl, "Logger"};

// MessageService
typedef struct {
  efd_fake_hdr h;
  uint32_t net_id;
  int qi_ok;           // QueryInterface(0x7000003) succeeds
  const void *helper;  // the object expected as the subscription callback
} efd_fake_ms;

static const char *efd_who(const efd_fake_ms *ms, const void *p) { return p == ms->helper ? "helper" : "other"; }

static bool efd_ms_qi(void *self, uint32_t id, void **out) {
  efd_fake_ms *ms = (efd_fake_ms *)self;
  bool ok = ms->qi_ok && id == 0x7000003u;
  efd_txf("ms.QueryInterface(%#x) -> %d\n", id, ok);
  *out = ok ? self : NULL;
  return ok;
}
static uint32_t efd_ms_get_net_id(void *self) {
  efd_txf("ms.GetNetID -> %#x\n", ((efd_fake_ms *)self)->net_id);
  return ((efd_fake_ms *)self)->net_id;
}
static void efd_ms_subscribe(void *self, void *cb, const uint64_t *cat, uint32_t a, uint32_t b) {
  efd_txf("ms.Subscribe(%s, %016llx, %u, %u)\n", efd_who((efd_fake_ms *)self, cb), (unsigned long long)*cat, a, b);
}
static void efd_ms_unsubscribe(void *self, void *cb, const uint64_t *cat, uint32_t a) {
  efd_txf("ms.Unsubscribe(%s, %016llx, %u)\n", efd_who((efd_fake_ms *)self, cb), (unsigned long long)*cat, a);
}
static void *efd_ms_vtbl[EFD_FAKE_SLOTS];

// ServiceManager
typedef struct {
  efd_fake_hdr h;
  void *message_service;  // returned for ClassID 0x7000003, NULL otherwise
} efd_fake_sm;

static void *efd_sm_get(void *self, uint32_t id) {
  efd_fake_sm *sm = (efd_fake_sm *)self;
  void *r = id == 0x7000003u ? sm->message_service : NULL;
  efd_txf("sm.GetSystemService(%#x) -> %s\n", id, r ? "MessageService" : "null");
  return r;
}
static void *efd_sm_vtbl[EFD_FAKE_SLOTS];

static void efd_fakes_init(void) {
  efd_fill_unexpected(efd_logger_vtbl);
  efd_logger_vtbl[11] = (void *)efd_fake_log;
  efd_logger_vtbl[23] = (void *)efd_fake_is_logging;
  efd_fill_unexpected(efd_ms_vtbl);
  efd_ms_vtbl[4] = (void *)efd_ms_qi;
  efd_ms_vtbl[25] = (void *)efd_ms_get_net_id;
  efd_ms_vtbl[27] = (void *)efd_ms_subscribe;
  efd_ms_vtbl[28] = (void *)efd_ms_unsubscribe;
  efd_fill_unexpected(efd_sm_vtbl);
  efd_sm_vtbl[15] = (void *)efd_sm_get;
}

#endif
