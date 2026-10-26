// Adapters of override module `efd_ds` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_efd.h"

/* 0x1012c1fe0 rsg_efd_datastream_init */
void F_1012c1fe0(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  rsg_efd_datastream_init(p0, p1);
  ov_ret_void(c);
}

/* 0x1012c2080 rsg_efd_datastream_init_copy */
void F_1012c2080(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const void * p1 = (const void *)(uintptr_t)OV_U64(a);
  _Bool p2 = (_Bool)(OV_U64(a) & 0xff);
  rsg_efd_datastream_init_copy(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x1012c2130 rsg_efd_datastream_destroy */
void F_1012c2130(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  rsg_efd_datastream_destroy(p0);
  ov_ret_void(c);
}

/* 0x1012c2200 rsg_efd_datastream_read */
void F_1012c2200(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  rsg_efd_datastream_read(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x1012c2300 rsg_efd_datastream_write */
void F_1012c2300(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const void * p1 = (const void *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  rsg_efd_datastream_write(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x1012c2460 rsg_efd_datastream_seek */
void F_1012c2460(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  size_t p1 = (size_t)OV_U64(a);
  _Bool r = rsg_efd_datastream_seek(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x1012c2550 rsg_efd_datastream_write_stream */
void F_1012c2550(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  rsg_efd_datastream_write_stream(p0, p1);
  ov_ret_void(c);
}

/* 0x1012c26b0 rsg_efd_datastream_read_stream */
void F_1012c26b0(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  rsg_efd_datastream_read_stream(p0, p1);
  ov_ret_void(c);
}

/* 0x1013036c0 rsg_efd_datastream_write_id */
void F_1013036c0(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const uint64_t * p1 = (const uint64_t *)(uintptr_t)OV_U64(a);
  void * r = rsg_efd_datastream_write_id(p0, p1);
  OV_RET_PTR(c, r);
}

/* 0x101303770 rsg_efd_datastream_read_id */
void F_101303770(rc_ctx *c) {
  OV_ARGS(a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  uint64_t * p1 = (uint64_t *)(uintptr_t)OV_U64(a);
  void * r = rsg_efd_datastream_read_id(p0, p1);
  OV_RET_PTR(c, r);
}
