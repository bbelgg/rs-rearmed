// Rewrites of Gamebryo LightSpeed efd (Foundation) functions.
//
// Every function names the original's address in the decrypted x86_64 binary.
// The C entry points take the same arguments as the original (a member
// function's `this` is the first parameter) and work on objects with the
// original's memory layout, so the differential test can call the original
// and the rewrite through one table of function pointers (test/efd_cases.h).
// Behaviour is described in src/efd_*.c*.
#ifndef RS_EFD_H
#define RS_EFD_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Logging -----------------------------------------------------------------
// The originals report errors through efd::Logger (module, level, __FILE__,
// __LINE__, message). The rewrites call this hook instead; NULL drops them.
typedef void (*rsg_efd_log_fn)(int module, int level, const char *file, int line, const char *msg);
void rsg_efd_set_log(rsg_efd_log_fn fn);

// --- efd::UniversalID / efd::Category (UniversalID.cpp) ----------------------
// A 64-bit packed ID. Bit 63 set: a structured ID with type in bits 61-62 and
// usage in bits 59-60; the other fields depend on the type (see efd_id.c).
bool rsg_efd_id_set_type(uint64_t *id, int type);           // 0x101303210
bool rsg_efd_id_set_usage(uint64_t *id, int usage);         // 0x101303330
bool rsg_efd_id_set_shard(uint64_t *id, uint32_t shard);    // 0x101303450
bool rsg_efd_id_set_net_id(uint64_t *id, uint32_t net_id);  // 0x1013035a0
bool rsg_efd_id_set_base_id(uint64_t *id, uint64_t base);   // 0x101303610
// Category(ClassID-like base ID, net ID) with usage 0 / usage 2.
uint64_t *rsg_efd_category_init_u0(uint64_t *cat, uint32_t base, uint32_t net);  // 0x1012f44b0
uint64_t *rsg_efd_category_init_u2(uint64_t *cat, uint32_t base, uint32_t net);  // 0x1012f45a0
// Category(IMessageService*, base): net ID from the service's GetNetID (slot 25).
uint64_t *rsg_efd_category_init_svc(uint64_t *cat, void *message_service, uint32_t base);  // 0x1012f4520

// --- efd::FixedString / efd::GlobalStringTable -------------------------------
// A FixedString is one pointer to interned character data. The 16-byte
// header in front of the data holds the reference count (+0) and
// (bucket << 16 | length) (+8). The table has 513 buckets keyed by
// `h = h * 33 + (signed char)c` over the string, mod 513.
typedef struct {
  const char *handle;
} rsg_efd_fixedstring;
void rsg_efd_string_table_create(void);   // 0x1012e3ce0 (+ ctor 0x1012e3d90)
void rsg_efd_string_table_destroy(void);  // 0x1012e3d10
void rsg_efd_fixedstring_init(rsg_efd_fixedstring *s);                       // 0x1012e2320
void rsg_efd_fixedstring_init_cstr(rsg_efd_fixedstring *s, const char *str);  // 0x1012e2340
void rsg_efd_fixedstring_init_copy(rsg_efd_fixedstring *s, const rsg_efd_fixedstring *o);  // 0x1012e23a0
void rsg_efd_fixedstring_release(rsg_efd_fixedstring *s);                    // 0x1012e23e0 (dtor)
rsg_efd_fixedstring *rsg_efd_fixedstring_assign(rsg_efd_fixedstring *s, const rsg_efd_fixedstring *o);  // 0x1012e2490
const char *rsg_efd_fixedstring_c_str(const rsg_efd_fixedstring *s);           // 0x1012e2480
bool rsg_efd_fixedstring_exists(const rsg_efd_fixedstring *s);                 // 0x1012e2510
uint32_t rsg_efd_fixedstring_length(const rsg_efd_fixedstring *s);             // 0x1012e2530
bool rsg_efd_fixedstring_equals(const rsg_efd_fixedstring *s, const char *str);  // 0x1012e2550
// Table statistics the original keeps in globals 0x101939c78 / 0x101939c80.
uint64_t rsg_efd_string_table_count(void);
uint64_t rsg_efd_string_table_collisions(void);
uint32_t rsg_efd_string_table_bucket_size(uint32_t bucket);

// --- efd::DataStream (DataStream.cpp) ----------------------------------------
// Growable byte buffer used for message serialization. sizeof 0x38, layout as
// the original: vptr, int32 +0x08, uint8_t *buf +0x10, uint32 cap +0x18,
// uint32 size +0x1c, uint8_t *pos +0x20, endian block +0x28 (shared,
// refcounted; int32 +8 is 1 for little-endian), bool read_only +0x30,
// bool owns_buffer +0x31.
#define RSG_EFD_DATASTREAM_SIZE 0x38
void rsg_efd_datastream_init(void *ds, uint32_t capacity);                  // 0x1012c1fe0
void rsg_efd_datastream_init_copy(void *ds, const void *other, bool ro);   // 0x1012c2080
void rsg_efd_datastream_destroy(void *ds);                                  // 0x1012c2130 (D1)
void rsg_efd_datastream_read(void *ds, void *dst, size_t n);                // 0x1012c2200
void rsg_efd_datastream_write(void *ds, const void *src, size_t n);         // 0x1012c2300
bool rsg_efd_datastream_seek(void *ds, size_t offset);                      // 0x1012c2460
void rsg_efd_datastream_write_stream(void *ds, void *other);                // 0x1012c2550
void rsg_efd_datastream_read_stream(void *ds, void *other);                 // 0x1012c26b0
void *rsg_efd_datastream_write_id(void *ds, const uint64_t *id);            // 0x1013036c0
void *rsg_efd_datastream_read_id(void *ds, uint64_t *id);                   // 0x101303770

// --- UTF-8 and string helpers ------------------------------------------------
// efd::utf8string layout (0x20 bytes): {union {char *heap; char inline[16]};
// char *end; char *cap}, where cap == &end marks the inline buffer.
typedef struct {
  union {
    char *heap;
    char sso[16];
  } u;
  char *end;
  char *cap;
} rsg_efd_utf8string;
int rsg_efd_utf8_units_at(const char *lead);  // 0x101303810: bytes in the sequence led by *lead
int rsg_efd_utf8_units(uint64_t lead);        // 0x101303830: same, lead byte by value
int rsg_efd_utf8_tail_missing(const char *s, uint32_t n);  // 0x101303850
size_t rsg_efd_utf8string_length(const rsg_efd_utf8string *s);         // 0x1013050e0 (code points)
rsg_efd_utf8string *rsg_efd_utf8string_to_lower(rsg_efd_utf8string *s);  // 0x1013064d0
rsg_efd_utf8string *rsg_efd_utf8string_to_upper(rsg_efd_utf8string *s);  // 0x101306460
bool rsg_efd_path_standardize(char *path);  // 0x1012f6d60: '\\'->'/', drop repeated '/', lowercase
int rsg_efd_snprintf(char *buf, size_t count, const char *fmt, ...);  // 0x10130b200
int rsg_efd_vsnprintf(char *buf, size_t count, const char *fmt, va_list ap);
// efd's fast-sqrt mantissa table (0x1012f8d60, stored in 0x10193e6f8 by efd::Init).
void rsg_efd_fill_sqrt_table(uint32_t table[256]);

// --- efd class info: QueryInterface / GetClassID -----------------------------
// Each efd class has a static table {uint32 count; entries[count] of 0x20
// bytes: {uint32 class_id; ...; uint32 offset at +0x18}}, filled by a static
// initializer: the class itself first, then its interfaces. Slot 6 of the
// class's vtable returns it. `object` is the complete object: QueryInterface
// through a secondary base's vtable is `this` + the vtable's offset-to-top.
bool rsg_efd_query_interface(const void *class_desc, void *object, uint32_t id, void **out);  // IBase slot 4/5
uint32_t rsg_efd_class_desc_id(const void *class_desc);  // IBase slot 7 (0x10001cb60) without the vcall

// --- efd::IMessageHelperSystemService (IMessageHelperSystemService.cpp) ------
// sizeof 0x20: vptr, ServiceManager* +0x08, int32 priority +0x10 (2000),
// uint32 message class ID +0x14, uint32 net ID +0x18.
#define RSG_EFD_MSGHELPER_SIZE 0x20
void rsg_efd_msghelper_init(void *self, uint32_t class_id);           // 0x1012e5470
int rsg_efd_msghelper_on_preinit(void *self);  // slot 10, 0x1012e54a0: 1 ok, 0 no MessageService
int rsg_efd_msghelper_on_shutdown(void *self);                        // slot 13, 0x1012e5680
void rsg_efd_msghelper_on_net_id(void *self, const void *message);    // 0x1012e53a0
int rsg_efd_msghelper_on_init(void *self);                            // slot 11, 0x1012e5ba0
int rsg_efd_msghelper_on_tick(void *self);                            // slot 12, 0x1012e5bb0
const char *rsg_efd_msghelper_display_name(void *self);               // slot 14, 0x1012e5bd0

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// The efd interfaces the rewrites call through, declared in the original's
// slot order so that a C++ call compiles to the same `call *off(%reg)` as the
// original (and works on objects the original code created).
namespace rsg {
namespace efd {

typedef uint32_t ClassID;

struct Category {
  uint64_t id;
};

class IBase {
 public:
  virtual void Destroy() = 0;                                   // 0
  virtual void IncRefCount() = 0;                               // 1
  virtual void DecRefCount() = 0;                               // 2
  virtual uint32_t GetRefCount() const = 0;                     // 3
  virtual bool QueryInterface(ClassID id, void **out) = 0;      // 4
  virtual bool QueryInterface(ClassID id, const void **out) const = 0;  // 5
  virtual const void *GetClassDesc() const = 0;                 // 6
  virtual ClassID GetClassID() const = 0;                       // 7
  virtual void Destruct() = 0;                                  // 8 (D1)
  virtual void DeletingDestruct() = 0;                          // 9 (D0)
};

class ISystemService : public IBase {
 public:
  virtual int OnPreInit() = 0;                // 10
  virtual int OnInit() = 0;                   // 11
  virtual int OnTick() = 0;                   // 12
  virtual int OnShutdown() = 0;               // 13
  virtual const char *GetDisplayName() = 0;   // 14
};

// ServiceManager vtable 0x101861a78: slot 15 is GetSystemService (0x1012fd300,
// a std::map<ClassID, ISystemService*> lookup), called as *0x78.
class ServiceManager {
 public:
  virtual void Slot0() = 0;
  virtual void Slot1() = 0;
  virtual void Slot2() = 0;
  virtual void Slot3() = 0;
  virtual void Slot4() = 0;
  virtual void Slot5() = 0;
  virtual void Slot6() = 0;
  virtual void Slot7() = 0;
  virtual void Slot8() = 0;
  virtual void Slot9() = 0;
  virtual void Slot10() = 0;
  virtual void Slot11() = 0;
  virtual void Slot12() = 0;
  virtual void Slot13() = 0;
  virtual void Slot14() = 0;
  virtual ISystemService *GetSystemService(ClassID id) = 0;  // 15
};

// MessageService (vtable 0x101861730, ClassID 0x7000003), the slots the
// message helper uses.
class IMessageService : public ISystemService {
 public:
  virtual void Slot15() = 0;
  virtual void Slot16() = 0;
  virtual void Slot17() = 0;
  virtual void Slot18() = 0;
  virtual void Slot19() = 0;
  virtual void Slot20() = 0;
  virtual void Slot21() = 0;
  virtual void Slot22() = 0;
  virtual void Slot23() = 0;
  virtual void Slot24() = 0;
  virtual uint32_t GetNetID() = 0;                                              // 25
  virtual void Slot26() = 0;
  virtual void Subscribe(void *callback, const Category &cat, uint32_t qos, uint32_t flags) = 0;  // 27
  virtual void Unsubscribe(void *callback, const Category &cat, uint32_t qos) = 0;              // 28
};

enum : ClassID {
  kCLASSID_IBase = 0xffffffffu,
  kCLASSID_ISystemService = 0x7000001u,
  kCLASSID_MessageService = 0x7000003u,
  kCLASSID_Helper7000002 = 0x7000002u,  // third subscription's base ID; no class has this ID
};

}  // namespace efd
}  // namespace rsg
#endif

#endif
