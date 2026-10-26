// efd::IMessageHelperSystemService (efd/IMessageHelperSystemService.cpp): the
// base of system services that receive messages of one class, e.g.
// EntityLoaderService (vtable 0x10185f4c8 reuses its OnPreInit). Abstract
// vtable 0x1018614f8.
//
// Layout (sizeof 0x20): vptr, ServiceManager *m_pServiceManager (+0x08, set
// when the service is registered), int32 priority (+0x10, 2000), uint32 the
// message class ID (+0x14), uint32 the net ID it subscribed with (+0x18).
//
// It looks MessageService up through the ServiceManager (GetSystemService,
// slot 15) and IBase::QueryInterface, and subscribes itself to three
// categories:
//   Category(usage 2, base = class ID, net = MessageService::GetNetID())
//   Category(usage 0, base = class ID, net = 0xffe)
//   Category(usage 0, base = 0x7000002, net = 0xffe)
// When MessageService announces a new net ID (+0x3c of the message) it
// re-subscribes the first one.
#include "efd_internal.h"

namespace rsg {
namespace efd {

static const uint32_t kAnyNet = 0xffe;

struct MessageHelper {
  void *vptr;
  ServiceManager *serviceManager;
  int32_t priority;
  uint32_t classID;
  uint32_t netID;
};

static Category MakeCategory(int usage, uint32_t base, uint32_t net) {
  Category c;
  if (usage == 2) rsg_efd_category_init_u2(&c.id, base, net);
  else rsg_efd_category_init_u0(&c.id, base, net);
  return c;
}

static IMessageService *FindMessageService(MessageHelper *h) {
  ISystemService *s = h->serviceManager->GetSystemService(kCLASSID_MessageService);
  void *ms = NULL;
  if (!s || !s->QueryInterface(kCLASSID_MessageService, &ms)) return NULL;
  return (IMessageService *)ms;
}

}  // namespace efd
}  // namespace rsg

using namespace rsg::efd;

extern "C" {

// 0x1012f4520
uint64_t *rsg_efd_category_init_svc(uint64_t *cat, void *message_service, uint32_t base) {
  uint32_t net = ((IMessageService *)message_service)->GetNetID();
  return rsg_efd_category_init_u2(cat, base, net);
}

// 0x1012e5470. The rewrite has no vtable of its own: the slots are the
// functions below.
void rsg_efd_msghelper_init(void *self, uint32_t class_id) {
  MessageHelper *h = (MessageHelper *)self;
  h->serviceManager = NULL;
  h->priority = 2000;
  h->classID = class_id;
  h->netID = 0;
}

// 0x1012e54a0 (slot 10). 1 on success; 0 and an error if there is no
// MessageService.
int rsg_efd_msghelper_on_preinit(void *self) {
  MessageHelper *h = (MessageHelper *)self;
  IMessageService *ms = FindMessageService(h);
  if (!ms) {
    rsg_efd_logf(RSG_EFD_LOG_MESSAGES, RSG_EFD_ERR2, RSG_EFD_SRC "IMessageHelperSystemService.cpp", 0x33,
                 "OnPreInit failed to find MessageService to register subscriptions!");
    return 0;
  }
  h->netID = ms->GetNetID();
  ms->Subscribe(h, MakeCategory(2, h->classID, h->netID), 0, 0);
  ms->Subscribe(h, MakeCategory(0, h->classID, kAnyNet), 0, 0);
  ms->Subscribe(h, MakeCategory(0, kCLASSID_Helper7000002, kAnyNet), 0, 0);
  return 1;
}

// 0x1012e5680 (slot 13). Always 2 (done), with or without MessageService.
int rsg_efd_msghelper_on_shutdown(void *self) {
  MessageHelper *h = (MessageHelper *)self;
  if (IMessageService *ms = FindMessageService(h)) {
    ms->Unsubscribe(h, MakeCategory(2, h->classID, h->netID), 0);
    ms->Unsubscribe(h, MakeCategory(0, h->classID, kAnyNet), 0);
    ms->Unsubscribe(h, MakeCategory(0, kCLASSID_Helper7000002, kAnyNet), 0);
  }
  return 2;
}

// 0x1012e53a0: the net ID announcement handler.
void rsg_efd_msghelper_on_net_id(void *self, const void *message) {
  MessageHelper *h = (MessageHelper *)self;
  IMessageService *ms = FindMessageService(h);
  if (!ms) return;
  uint32_t net = *(const uint32_t *)((const uint8_t *)message + 0x3c);
  if (net == h->netID) return;
  ms->Unsubscribe(h, MakeCategory(2, h->classID, h->netID), 0);
  h->netID = net;
  ms->Subscribe(h, MakeCategory(2, h->classID, h->netID), 0, 0);
}

int rsg_efd_msghelper_on_init(void *) { return 2; }  // 0x1012e5ba0 (slot 11)
int rsg_efd_msghelper_on_tick(void *) { return 2; }  // 0x1012e5bb0 (slot 12)
const char *rsg_efd_msghelper_display_name(void *) { return "Unknown_Service"; }  // 0x1012e5bd0

}  // extern "C"
