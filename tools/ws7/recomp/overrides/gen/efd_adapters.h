// Adapter table of override module `efd` (generated, checked in).
#ifndef OV_GEN_EFD_H
#define OV_GEN_EFD_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(rsg_ak_get_id_from_string)(rc_ctx *c);
void RC_FN(rsg_efd_msghelper_on_init)(rc_ctx *c);
void RC_FN(rsg_efd_msghelper_on_tick)(rc_ctx *c);
void RC_FN(rsg_efd_msghelper_display_name)(rc_ctx *c);
void RC_FN(rsg_efd_category_init_u0)(rc_ctx *c);
void RC_FN(rsg_efd_category_init_u2)(rc_ctx *c);
void RC_FN(rsg_efd_path_standardize)(rc_ctx *c);
void RC_FN(efd_sqrt_table_new)(rc_ctx *c);
void RC_FN(rsg_efd_id_set_type)(rc_ctx *c);
void RC_FN(rsg_efd_id_set_usage)(rc_ctx *c);
void RC_FN(rsg_efd_id_set_shard)(rc_ctx *c);
void RC_FN(rsg_efd_id_set_net_id)(rc_ctx *c);
void RC_FN(rsg_efd_id_set_base_id)(rc_ctx *c);
void RC_FN(rsg_efd_utf8_units_at)(rc_ctx *c);
void RC_FN(rsg_efd_utf8_units)(rc_ctx *c);
void RC_FN(rsg_efd_utf8_tail_missing)(rc_ctx *c);
void RC_FN(rsg_efd_utf8string_length)(rc_ctx *c);
void RC_FN(rsg_efd_utf8string_to_upper)(rc_ctx *c);
void RC_FN(rsg_efd_utf8string_to_lower)(rc_ctx *c);
void RC_FN(efd_snprintf)(rc_ctx *c);
static const ov_entry ov_efd_entries[] = {
  {RC_ADDR(rsg_ak_get_id_from_string), "rsg_ak_get_id_from_string", RC_FN(rsg_ak_get_id_from_string)},
  {RC_ADDR(rsg_efd_msghelper_on_init), "rsg_efd_msghelper_on_init", RC_FN(rsg_efd_msghelper_on_init)},
  {RC_ADDR(rsg_efd_msghelper_on_tick), "rsg_efd_msghelper_on_tick", RC_FN(rsg_efd_msghelper_on_tick)},
  {RC_ADDR(rsg_efd_msghelper_display_name), "rsg_efd_msghelper_display_name", RC_FN(rsg_efd_msghelper_display_name)},
  {RC_ADDR(rsg_efd_category_init_u0), "rsg_efd_category_init_u0", RC_FN(rsg_efd_category_init_u0)},
  {RC_ADDR(rsg_efd_category_init_u2), "rsg_efd_category_init_u2", RC_FN(rsg_efd_category_init_u2)},
  {RC_ADDR(rsg_efd_path_standardize), "rsg_efd_path_standardize", RC_FN(rsg_efd_path_standardize)},
  {RC_ADDR(efd_sqrt_table_new), "efd_sqrt_table_new", RC_FN(efd_sqrt_table_new)},
  {RC_ADDR(rsg_efd_id_set_type), "rsg_efd_id_set_type", RC_FN(rsg_efd_id_set_type)},
  {RC_ADDR(rsg_efd_id_set_usage), "rsg_efd_id_set_usage", RC_FN(rsg_efd_id_set_usage)},
  {RC_ADDR(rsg_efd_id_set_shard), "rsg_efd_id_set_shard", RC_FN(rsg_efd_id_set_shard)},
  {RC_ADDR(rsg_efd_id_set_net_id), "rsg_efd_id_set_net_id", RC_FN(rsg_efd_id_set_net_id)},
  {RC_ADDR(rsg_efd_id_set_base_id), "rsg_efd_id_set_base_id", RC_FN(rsg_efd_id_set_base_id)},
  {RC_ADDR(rsg_efd_utf8_units_at), "rsg_efd_utf8_units_at", RC_FN(rsg_efd_utf8_units_at)},
  {RC_ADDR(rsg_efd_utf8_units), "rsg_efd_utf8_units", RC_FN(rsg_efd_utf8_units)},
  {RC_ADDR(rsg_efd_utf8_tail_missing), "rsg_efd_utf8_tail_missing", RC_FN(rsg_efd_utf8_tail_missing)},
  {RC_ADDR(rsg_efd_utf8string_length), "rsg_efd_utf8string_length", RC_FN(rsg_efd_utf8string_length)},
  {RC_ADDR(rsg_efd_utf8string_to_upper), "rsg_efd_utf8string_to_upper", RC_FN(rsg_efd_utf8string_to_upper)},
  {RC_ADDR(rsg_efd_utf8string_to_lower), "rsg_efd_utf8string_to_lower", RC_FN(rsg_efd_utf8string_to_lower)},
  {RC_ADDR(efd_snprintf), "efd_snprintf", RC_FN(efd_snprintf)},
};
#define OV_EFD_COUNT 20
#endif
