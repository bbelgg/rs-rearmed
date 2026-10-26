// Adapters of override module `png` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "png.h"
#include "png_glue.h"

/* 0x100b58c70 png_set_sig_bytes */
void F_100b58c70(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  png_set_sig_bytes(p0, p1);
  ov_ret_void(c);
}

/* 0x100b58ca0 png_get_io_ptr */
void F_100b58ca0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_voidp r = png_get_io_ptr(p0);
  OV_RET_PTR(c, r);
}

/* 0x100b5aea0 png_sig_cmp */
void F_100b5aea0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_const_bytep p0 = (png_const_bytep)(uintptr_t)OV_U64(a);
  png_size_t p1 = (png_size_t)OV_U64(a);
  png_size_t p2 = (png_size_t)OV_U64(a);
  int r = png_sig_cmp(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100b5b170 png_create_info_struct */
void F_100b5b170(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop r = png_create_info_struct(p0);
  OV_RET_PTR(c, r);
}

/* 0x100b5c4a0 png_get_valid */
void F_100b5c4a0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_const_infop p1 = (png_const_infop)(uintptr_t)OV_U64(a);
  png_uint_32 p2 = (png_uint_32)OV_U64(a);
  png_uint_32 r = png_get_valid(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100b5c4c0 png_get_rowbytes */
void F_100b5c4c0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_const_infop p1 = (png_const_infop)(uintptr_t)OV_U64(a);
  png_size_t r = png_get_rowbytes(p0, p1);
  OV_RET_U64(c, r);
}

/* 0x100b5c4e0 png_get_gAMA */
void F_100b5c4e0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_const_infop p1 = (png_const_infop)(uintptr_t)OV_U64(a);
  double * p2 = (double *)(uintptr_t)OV_U64(a);
  png_uint_32 r = png_get_gAMA(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100b5c510 png_get_IHDR */
void F_100b5c510(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_uint_32 * p2 = (png_uint_32 *)(uintptr_t)OV_U64(a);
  png_uint_32 * p3 = (png_uint_32 *)(uintptr_t)OV_U64(a);
  int * p4 = (int *)(uintptr_t)OV_U64(a);
  int * p5 = (int *)(uintptr_t)OV_U64(a);
  int * p6 = (int *)(uintptr_t)OV_U64(a);
  int * p7 = (int *)(uintptr_t)OV_U64(a);
  int * p8 = (int *)(uintptr_t)OV_U64(a);
  png_uint_32 r = png_get_IHDR(p0, p1, p2, p3, p4, p5, p6, p7, p8);
  OV_RET_I32(c, r);
}

/* 0x100b61590 png_create_read_struct */
void F_100b61590(rc_ctx *c) {
  OV_ARGS(a, c);
  png_const_charp p0 = (png_const_charp)(uintptr_t)OV_U64(a);
  png_voidp p1 = (png_voidp)(uintptr_t)OV_U64(a);
  png_error_ptr p2 = OV_FN(png_error_ptr, OV_U64(a));
  png_error_ptr p3 = OV_FN(png_error_ptr, OV_U64(a));
  png_structp r = png_create_read_struct(p0, p1, p2, p3);
  OV_RET_PTR(c, r);
}

/* 0x100b61780 png_destroy_read_struct */
void F_100b61780(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structpp p0 = (png_structpp)(uintptr_t)OV_U64(a);
  png_infopp p1 = (png_infopp)(uintptr_t)OV_U64(a);
  png_infopp p2 = (png_infopp)(uintptr_t)OV_U64(a);
  png_destroy_read_struct(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100b61880 png_read_end */
void F_100b61880(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_read_end(p0, p1);
  ov_ret_void(c);
}

/* 0x100b61c90 png_read_row */
void F_100b61c90(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_bytep p1 = (png_bytep)(uintptr_t)OV_U64(a);
  png_bytep p2 = (png_bytep)(uintptr_t)OV_U64(a);
  png_read_row(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100b62170 png_read_image */
void F_100b62170(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_bytepp p1 = (png_bytepp)(uintptr_t)OV_U64(a);
  png_read_image(p0, p1);
  ov_ret_void(c);
}

/* 0x100b62240 png_read_update_info */
void F_100b62240(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_read_update_info(p0, p1);
  ov_ret_void(c);
}

/* 0x100b62270 png_read_info */
void F_100b62270(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_read_info(p0, p1);
  ov_ret_void(c);
}

/* 0x100b62970 png_set_read_fn */
void F_100b62970(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_voidp p1 = (png_voidp)(uintptr_t)OV_U64(a);
  png_rw_ptr p2 = OV_FN(png_rw_ptr, OV_U64(a));
  png_set_read_fn(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100b62a30 png_set_strip_16 */
void F_100b62a30(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_strip_16(p0);
  ov_ret_void(c);
}

/* 0x100b62a50 png_set_palette_to_rgb */
void F_100b62a50(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_palette_to_rgb(p0);
  ov_ret_void(c);
}

/* 0x100b62a70 png_set_expand_gray_1_2_4_to_8 */
void F_100b62a70(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_expand_gray_1_2_4_to_8(p0);
  ov_ret_void(c);
}

/* 0x100b62a90 png_set_tRNS_to_alpha */
void F_100b62a90(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_tRNS_to_alpha(p0);
  ov_ret_void(c);
}

/* 0x100b62ab0 png_set_gray_to_rgb */
void F_100b62ab0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_gray_to_rgb(p0);
  ov_ret_void(c);
}

/* 0x100b66cb0 png_set_gamma */
void F_100b66cb0(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  double p1 = OV_F64(a);
  double p2 = OV_F64(a);
  png_set_gamma(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100b67080 png_error */
void F_100b67080(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_const_charp p1 = (png_const_charp)(uintptr_t)OV_U64(a);
  png_error(p0, p1);
  ov_ret_void(c);
}

/* 0x100b67550 png_get_error_ptr */
void F_100b67550(rc_ctx *c) {
  OV_ARGS(a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_voidp r = png_get_error_ptr(p0);
  OV_RET_PTR(c, r);
}

/* 0x100b67570 png_longjmp */
void F_100b67570(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  png_longjmp(p0, p1);
  ov_ret_void(c);
}
