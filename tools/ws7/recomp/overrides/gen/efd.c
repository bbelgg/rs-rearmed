// Adapters of override module `efd` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_efd.h"
#include "rs_game.h"

/* 0x100ab08b0 rsg_ak_get_id_from_string */
void F_100ab08b0(rc_ctx *c) {
  OV_ARGS(a, c);
  const char * p0 = (const char *)(uintptr_t)OV_U64(a);
  uint32_t r = rsg_ak_get_id_from_string(p0);
  OV_RET_I32(c, r);
}

/* 0x1012e5ba0 rsg_efd_msghelper_on_init */
void F_1012e5ba0(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  int r = rsg_efd_msghelper_on_init(p0);
  OV_RET_I32(c, r);
}

/* 0x1012e5bb0 rsg_efd_msghelper_on_tick */
void F_1012e5bb0(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  int r = rsg_efd_msghelper_on_tick(p0);
  OV_RET_I32(c, r);
}

/* 0x1012e5bd0 rsg_efd_msghelper_display_name */
void F_1012e5bd0(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const char * r = rsg_efd_msghelper_display_name(p0);
  OV_RET_PTR(c, r);
}

/* 0x1012f44b0 rsg_efd_category_init_u0 */
void F_1012f44b0(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  uint32_t p2 = (uint32_t)OV_U64(a);
  uint64_t * r = rsg_efd_category_init_u0(p0, p1, p2);
  OV_RET_PTR(c, r);
}

/* 0x1012f45a0 rsg_efd_category_init_u2 */
void F_1012f45a0(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  uint32_t p2 = (uint32_t)OV_U64(a);
  uint64_t * r = rsg_efd_category_init_u2(p0, p1, p2);
  OV_RET_PTR(c, r);
}

/* 0x1012f6d60 rsg_efd_path_standardize */
void F_1012f6d60(rc_ctx *c) {
  OV_ARGS(a, c);
  char * p0 = (char *)(uintptr_t)OV_U64(a);
  _Bool r = rsg_efd_path_standardize(p0);
  OV_RET_I32(c, r);
}

/* 0x101303210 rsg_efd_id_set_type */
void F_101303210(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  _Bool r = rsg_efd_id_set_type(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x101303330 rsg_efd_id_set_usage */
void F_101303330(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  _Bool r = rsg_efd_id_set_usage(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x101303450 rsg_efd_id_set_shard */
void F_101303450(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  _Bool r = rsg_efd_id_set_shard(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x1013035a0 rsg_efd_id_set_net_id */
void F_1013035a0(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  _Bool r = rsg_efd_id_set_net_id(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x101303610 rsg_efd_id_set_base_id */
void F_101303610(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t * p0 = (uint64_t *)(uintptr_t)OV_U64(a);
  uint64_t p1 = (uint64_t)OV_U64(a);
  _Bool r = rsg_efd_id_set_base_id(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x101303810 rsg_efd_utf8_units_at */
void F_101303810(rc_ctx *c) {
  OV_ARGS(a, c);
  const char * p0 = (const char *)(uintptr_t)OV_U64(a);
  int r = rsg_efd_utf8_units_at(p0);
  OV_RET_I32(c, r);
}

/* 0x101303830 rsg_efd_utf8_units */
void F_101303830(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t p0 = (uint64_t)OV_U64(a);
  int r = rsg_efd_utf8_units(p0);
  OV_RET_I32(c, r);
}

/* 0x101303850 rsg_efd_utf8_tail_missing */
void F_101303850(rc_ctx *c) {
  OV_ARGS(a, c);
  const char * p0 = (const char *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  int r = rsg_efd_utf8_tail_missing(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x1013050e0 rsg_efd_utf8string_length */
void F_1013050e0(rc_ctx *c) {
  OV_ARGS(a, c);
  const rsg_efd_utf8string * p0 = (const rsg_efd_utf8string *)(uintptr_t)OV_U64(a);
  size_t r = rsg_efd_utf8string_length(p0);
  OV_RET_U64(c, r);
}

/* 0x101306460 rsg_efd_utf8string_to_upper */
void F_101306460(rc_ctx *c) {
  OV_ARGS(a, c);
  rsg_efd_utf8string * p0 = (rsg_efd_utf8string *)(uintptr_t)OV_U64(a);
  rsg_efd_utf8string * r = rsg_efd_utf8string_to_upper(p0);
  OV_RET_PTR(c, r);
}

/* 0x1013064d0 rsg_efd_utf8string_to_lower */
void F_1013064d0(rc_ctx *c) {
  OV_ARGS(a, c);
  rsg_efd_utf8string * p0 = (rsg_efd_utf8string *)(uintptr_t)OV_U64(a);
  rsg_efd_utf8string * r = rsg_efd_utf8string_to_lower(p0);
  OV_RET_PTR(c, r);
}
