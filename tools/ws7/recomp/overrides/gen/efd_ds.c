// Adapters of override module `efd_ds` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_efd.h"

void RC_FN(rsg_efd_datastream_init)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_init, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  rsg_efd_datastream_init(p0, p1);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_init_copy)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_init_copy, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const void * p1 = (const void *)(uintptr_t)OV_U64(a);
  _Bool p2 = (_Bool)(OV_U64(a) & 0xff);
  rsg_efd_datastream_init_copy(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_destroy)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_destroy, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  rsg_efd_datastream_destroy(p0);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_read)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_read, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  rsg_efd_datastream_read(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_write)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_write, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const void * p1 = (const void *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  rsg_efd_datastream_write(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_seek)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_seek, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  size_t p1 = (size_t)OV_U64(a);
  _Bool r = rsg_efd_datastream_seek(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_efd_datastream_write_stream)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_write_stream, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  rsg_efd_datastream_write_stream(p0, p1);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_read_stream)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_read_stream, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  rsg_efd_datastream_read_stream(p0, p1);
  ov_ret_void(c);
}

void RC_FN(rsg_efd_datastream_write_id)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_write_id, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  const uint64_t * p1 = (const uint64_t *)(uintptr_t)OV_U64(a);
  void * r = rsg_efd_datastream_write_id(p0, p1);
  OV_RET_PTR(c, r);
}

void RC_FN(rsg_efd_datastream_read_id)(rc_ctx *c) {
  OV_ARGS_OF(rsg_efd_datastream_read_id, a, c);
  void * p0 = (void *)(uintptr_t)OV_U64(a);
  uint64_t * p1 = (uint64_t *)(uintptr_t)OV_U64(a);
  void * r = rsg_efd_datastream_read_id(p0, p1);
  OV_RET_PTR(c, r);
}
