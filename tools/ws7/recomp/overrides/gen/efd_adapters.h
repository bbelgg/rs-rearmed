// Adapter table of override module `efd` (generated, checked in).
#ifndef OV_GEN_EFD_H
#define OV_GEN_EFD_H
#include "rc.h"
#include "rc_ov.h"
void F_100ab08b0(rc_ctx *c);
void F_1012e5ba0(rc_ctx *c);
void F_1012e5bb0(rc_ctx *c);
void F_1012e5bd0(rc_ctx *c);
void F_1012f44b0(rc_ctx *c);
void F_1012f45a0(rc_ctx *c);
void F_1012f6d60(rc_ctx *c);
void F_1012f8d60(rc_ctx *c);
void F_101303210(rc_ctx *c);
void F_101303330(rc_ctx *c);
void F_101303450(rc_ctx *c);
void F_1013035a0(rc_ctx *c);
void F_101303610(rc_ctx *c);
void F_101303810(rc_ctx *c);
void F_101303830(rc_ctx *c);
void F_101303850(rc_ctx *c);
void F_1013050e0(rc_ctx *c);
void F_101306460(rc_ctx *c);
void F_1013064d0(rc_ctx *c);
void F_10130b200(rc_ctx *c);
static const ov_entry ov_efd_entries[] = {
  {0x100ab08b0ull, "rsg_ak_get_id_from_string", F_100ab08b0},
  {0x1012e5ba0ull, "rsg_efd_msghelper_on_init", F_1012e5ba0},
  {0x1012e5bb0ull, "rsg_efd_msghelper_on_tick", F_1012e5bb0},
  {0x1012e5bd0ull, "rsg_efd_msghelper_display_name", F_1012e5bd0},
  {0x1012f44b0ull, "rsg_efd_category_init_u0", F_1012f44b0},
  {0x1012f45a0ull, "rsg_efd_category_init_u2", F_1012f45a0},
  {0x1012f6d60ull, "rsg_efd_path_standardize", F_1012f6d60},
  {0x1012f8d60ull, "efd_sqrt_table_new", F_1012f8d60},
  {0x101303210ull, "rsg_efd_id_set_type", F_101303210},
  {0x101303330ull, "rsg_efd_id_set_usage", F_101303330},
  {0x101303450ull, "rsg_efd_id_set_shard", F_101303450},
  {0x1013035a0ull, "rsg_efd_id_set_net_id", F_1013035a0},
  {0x101303610ull, "rsg_efd_id_set_base_id", F_101303610},
  {0x101303810ull, "rsg_efd_utf8_units_at", F_101303810},
  {0x101303830ull, "rsg_efd_utf8_units", F_101303830},
  {0x101303850ull, "rsg_efd_utf8_tail_missing", F_101303850},
  {0x1013050e0ull, "rsg_efd_utf8string_length", F_1013050e0},
  {0x101306460ull, "rsg_efd_utf8string_to_upper", F_101306460},
  {0x1013064d0ull, "rsg_efd_utf8string_to_lower", F_1013064d0},
  {0x10130b200ull, "efd_snprintf", F_10130b200},
};
#define OV_EFD_COUNT 20
#endif
