// Adapters of override module `jpeg` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "stdio.h"
#include "jpeglib.h"
#include "jpeg_glue.h"

/* 0x100b40bf0 jpeg_CreateDecompress */
void F_100b40bf0(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  ov_jpeg_fix_err(p0);
  jpeg_CreateDecompress(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100b41030 jpeg_finish_decompress */
void F_100b41030(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  boolean r = jpeg_finish_decompress(p0);
  OV_RET_I32(c, r);
}

/* 0x100b41100 jpeg_read_header */
void F_100b41100(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  boolean p1 = (boolean)OV_U64(a);
  ov_jpeg_fix(p0);
  int r = jpeg_read_header(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100b41180 jpeg_abort_decompress */
void F_100b41180(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  jpeg_abort_decompress(p0);
  ov_ret_void(c);
}

/* 0x100b41190 jpeg_destroy_decompress */
void F_100b41190(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  jpeg_destroy_decompress(p0);
  ov_ret_void(c);
}

/* 0x100b411a0 jpeg_start_decompress */
void F_100b411a0(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  boolean r = jpeg_start_decompress(p0);
  OV_RET_I32(c, r);
}

/* 0x100b413b0 jpeg_read_scanlines */
void F_100b413b0(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  JSAMPARRAY p1 = (JSAMPARRAY)(uintptr_t)OV_U64(a);
  JDIMENSION p2 = (JDIMENSION)OV_U64(a);
  ov_jpeg_fix(p0);
  JDIMENSION r = jpeg_read_scanlines(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100b42800 jpeg_read_coefficients */
void F_100b42800(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  ov_jpeg_fix(p0);
  jvirt_barray_ptr * r = jpeg_read_coefficients(p0);
  OV_RET_PTR(c, r);
}

/* 0x100b44270 jpeg_resync_to_restart */
void F_100b44270(rc_ctx *c) {
  OV_ARGS(a, c);
  j_decompress_ptr p0 = (j_decompress_ptr)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  ov_jpeg_fix(p0);
  boolean r = jpeg_resync_to_restart(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100b57920 jpeg_std_error */
void F_100b57920(rc_ctx *c) {
  OV_ARGS(a, c);
  struct jpeg_error_mgr * p0 = (struct jpeg_error_mgr *)(uintptr_t)OV_U64(a);
  struct jpeg_error_mgr * r = jpeg_std_error(p0);
  OV_RET_PTR(c, r);
}
