// Adapters of override module `efd` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_efd.h"
#include "rs_game.h"

void RC_FN(rsg_ak_get_id_from_string)(rc_ctx *c) {
  OV_ARGS_OF(rsg_ak_get_id_from_string, a, c);
  const char * p0 = (const char *)(uintptr_t)OV_U64(a);
  uint32_t r = rsg_ak_get_id_from_string(p0);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_msghelper_on_init)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_msghelper_on_init, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  int r = rsg_efd_msghelper_on_init(p0);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_msghelper_on_tick)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_msghelper_on_tick, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  int r = rsg_efd_msghelper_on_tick(p0);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_msghelper_display_name)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_msghelper_display_name, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const char * r = rsg_efd_msghelper_display_name(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(rsg_efd_category_init_u0)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_category_init_u0, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  uint32_t p2 = (uint32_t)OV_U64(a);
  uint64_t * r = rsg_efd_category_init_u0(p0, p1, p2);
  OV_RET_PTR(c, r);
}

void RC_FN(rsg_efd_category_init_u2)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_category_init_u2, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  uint32_t p2 = (uint32_t)OV_U64(a);
  uint64_t * r = rsg_efd_category_init_u2(p0, p1, p2);
  OV_RET_PTR(c, r);
}

void RC_FN(rsg_efd_path_standardize)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_path_standardize, a, c);
  char * p0 = (char *)(uintptr_t)OV_U64(a);
  _Bool r = rsg_efd_path_standardize(p0);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_id_set_type)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_id_set_type, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  _Bool r = rsg_efd_id_set_type(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_id_set_usage)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_id_set_usage, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  _Bool r = rsg_efd_id_set_usage(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_id_set_shard)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_id_set_shard, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  _Bool r = rsg_efd_id_set_shard(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_id_set_net_id)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_id_set_net_id, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  _Bool r = rsg_efd_id_set_net_id(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_id_set_base_id)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_id_set_base_id, a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint64_t p1 = (uint64_t)OV_U64(a);
  _Bool r = rsg_efd_id_set_base_id(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_utf8_units_at)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_utf8_units_at, a, c);
  const char * p0 = (const char *)(uintptr_t)OV_U64(a);
  int r = rsg_efd_utf8_units_at(p0);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_utf8_units)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_utf8_units, a, c);
  uint64_t p0 = (uint64_t)OV_U64(a);
  int r = rsg_efd_utf8_units(p0);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_utf8_tail_missing)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_utf8_tail_missing, a, c);
  const char * p0 = (const char *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  int r = rsg_efd_utf8_tail_missing(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_utf8string_length)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_utf8string_length, a, c);
  const rsg_efd_utf8string * p0 = (const rsg_efd_utf8string *)(uintptr_t)OV_U64(a);
  size_t r = rsg_efd_utf8string_length(p0);
  OV_RET_U64(c, r);
}

void RC_FN(rsg_efd_utf8string_to_upper)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_utf8string_to_upper, a, c);
  rsg_efd_utf8string * p0 = (rsg_efd_utf8string *)(uintptr_t)OV_U64(a);
  rsg_efd_utf8string * r = rsg_efd_utf8string_to_upper(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(rsg_efd_utf8string_to_lower)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_utf8string_to_lower, a, c);
  rsg_efd_utf8string * p0 = (rsg_efd_utf8string *)(uintptr_t)OV_U64(a);
  rsg_efd_utf8string * r = rsg_efd_utf8string_to_lower(p0);
  OV_RET_PTR(c, r);
}
