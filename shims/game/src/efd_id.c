// efd::UniversalID and efd::Category (Gamebryo LightSpeed, efd/UniversalID.cpp).
//
// A UniversalID is one uint64_t. Bit 63 clear: the low 63 bits are a plain ID.
// Bit 63 set: a structured ID,
//   bits 61-62  type (0..3)                  SetType   0x101303210
//   bits 59-60  usage (0..3)                 SetUsage  0x101303330
//   and per type:
//   type 0: base ID bits 0-60
//   type 1: base 0-18, net ID 19-50, shard 51-60
//   type 2: base 0-26, net ID 27-58                (a Category)
//   type 3: base 0-36, net ID 37-48 (12 bits), shard 49-58
// The setters for shard, net ID and base ID (0x101303450, 0x1013035a0,
// 0x101303610) mask the value into the field for the current type. Out-of-range
// type/usage/shard values are logged as errors and leave the ID unchanged.
//
// efd::Category is a type-2 UniversalID: its constructors start from
// 0xC000000000000000, set the usage, then the 32-bit net ID and the 27-bit base
// ID (the base is only stored if it fits). Messages are subscribed to by
// Category; the message helper (efd_msghelper.cpp) builds three of them.
#include "efd_internal.h"

#define ID_FILE RSG_EFD_SRC "UniversalID.cpp"

static unsigned id_type(uint64_t v) { return (unsigned)(v >> 61) & 3; }

bool rsg_efd_id_set_type(uint64_t *id, int type) {
  if (type >= 4) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR0, ID_FILE, 91,
                 "ID Error: Type value (%d) exceeds space for type bits (%d).", type, 2);
    return false;
  }
  *id = (*id & 0x9fffffffffffffffull) | ((uint64_t)(type & 3) << 61);
  return true;
}

bool rsg_efd_id_set_usage(uint64_t *id, int usage) {
  if (usage >= 4) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR0, ID_FILE, 116,
                 "ID Error: Usage value (%d) exceeds space for usage bits (%d).", usage, 2);
    return false;
  }
  *id = (*id & 0xe7ffffffffffffffull) | ((uint64_t)(usage & 3) << 59);
  return true;
}

bool rsg_efd_id_set_shard(uint64_t *id, uint32_t shard) {
  if (shard >= 0x400) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR0, ID_FILE, 149,
                 "ID Error: Shard value (%d) exceeds available space for shard ID bits (%d).",
                 (int)shard, 10);
    return false;
  }
  uint64_t v = *id;
  switch (id_type(v)) {
    case 3:
      *id = (v & 0xf801ffffffffffffull) | ((uint64_t)shard << 49);
      return true;
    case 1:
      *id = (v & 0xe007ffffffffffffull) | ((uint64_t)shard << 51);
      return true;
    default:
      return false;
  }
}

bool rsg_efd_id_set_net_id(uint64_t *id, uint32_t net) {
  uint64_t v = *id;
  switch (id_type(v)) {
    case 3:
      *id = (v & 0xfffe001fffffffffull) | ((uint64_t)(net & 0xfff) << 37);
      return true;
    case 2:
      *id = (v & 0xf800000007ffffffull) | ((uint64_t)net << 27);
      return true;
    case 1:
      *id = (v & 0xfff800000007ffffull) | ((uint64_t)net << 19);
      return true;
    default:
      return false;
  }
}

bool rsg_efd_id_set_base_id(uint64_t *id, uint64_t base) {
  uint64_t v = *id;
  if (!(v >> 63)) {
    *id = (v & 0x8000000000000000ull) | (base & 0x7fffffffffffffffull);
    return true;
  }
  static const uint64_t mask[4] = {0x1fffffffffffffffull, 0x7ffff, 0x7ffffff, 0x1fffffffffull};
  uint64_t m = mask[id_type(v)];
  *id = (v & ~m) | (base & m);
  return true;
}

static uint64_t *category_init(uint64_t *cat, int usage, uint32_t base, uint32_t net) {
  *cat = 0xc000000000000000ull;
  rsg_efd_id_set_usage(cat, usage);
  uint64_t v = (*cat & 0xf800000007ffffffull) | ((uint64_t)net << 27);
  if (base <= 0x7ffffff) v = (v & ~0x7ffffffull) | base;
  *cat = v;
  return cat;
}

uint64_t *rsg_efd_category_init_u0(uint64_t *cat, uint32_t base, uint32_t net) {
  return category_init(cat, 0, base, net);
}

uint64_t *rsg_efd_category_init_u2(uint64_t *cat, uint32_t base, uint32_t net) {
  return category_init(cat, 2, base, net);
}
