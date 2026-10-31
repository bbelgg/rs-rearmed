// Adapters of override module `jpeg` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "stdio.h"
#include "jpeglib.h"
#include "jpeg_glue.h"

void RC_FN(jpeg_CreateDecompress)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_CreateDecompress, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  ov_jpeg_fix_err(p0);
  jpeg_CreateDecompress(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(jpeg_finish_decompress)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_finish_decompress, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  boolean r = jpeg_finish_decompress(p0);
  OV_RET_I32(c, r);
}

void RC_FN(jpeg_read_header)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_read_header, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  boolean p1 = (boolean)OV_U64(a);
  ov_jpeg_fix(p0);
  int r = jpeg_read_header(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(jpeg_abort_decompress)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_abort_decompress, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  jpeg_abort_decompress(p0);
  ov_ret_void(c);
}

void RC_FN(jpeg_destroy_decompress)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_destroy_decompress, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  jpeg_destroy_decompress(p0);
  ov_ret_void(c);
}

void RC_FN(jpeg_start_decompress)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_start_decompress, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  boolean r = jpeg_start_decompress(p0);
  OV_RET_I32(c, r);
}

void RC_FN(jpeg_read_scanlines)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_read_scanlines, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  JSAMPARRAY p1 = (JSAMPARRAY)(uintptr_t)OV_U64(a);
  JDIMENSION p2 = (JDIMENSION)OV_U64(a);
  ov_jpeg_fix(p0);
  JDIMENSION r = jpeg_read_scanlines(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(jpeg_read_coefficients)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_read_coefficients, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  jvirt_barray_ptr * r = jpeg_read_coefficients(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(jpeg_resync_to_restart)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_resync_to_restart, a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  ov_jpeg_fix(p0);
  boolean r = jpeg_resync_to_restart(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(jpeg_std_error)(rc_ctx *c) {
  OV_ARGS_OF(jpeg_std_error, a, c);
  struct jpeg_error_mgr * p0 = (struct jpeg_error_mgr *)(uintptr_t)OV_U64(a);
  struct jpeg_error_mgr * r = jpeg_std_error(p0);
  OV_RET_PTR(c, r);
}
