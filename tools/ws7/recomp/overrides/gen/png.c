// Adapters of override module `png` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "png.h"
#include "png_glue.h"

void RC_FN(png_set_sig_bytes)(rc_ctx *c) {
  OV_ARGS_OF(png_set_sig_bytes, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  png_set_sig_bytes(p0, p1);
  ov_ret_void(c);
}

void RC_FN(png_get_io_ptr)(rc_ctx *c) {
  OV_ARGS_OF(png_get_io_ptr, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_voidp r = png_get_io_ptr(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(png_sig_cmp)(rc_ctx *c) {
  OV_ARGS_OF(png_sig_cmp, a, c);
  png_const_bytep p0 = (png_const_bytep)(uintptr_t)OV_U64(a);
  png_size_t p1 = (png_size_t)OV_U64(a);
  png_size_t p2 = (png_size_t)OV_U64(a);
  int r = png_sig_cmp(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(png_create_info_struct)(rc_ctx *c) {
  OV_ARGS_OF(png_create_info_struct, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop r = png_create_info_struct(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(png_get_valid)(rc_ctx *c) {
  OV_ARGS_OF(png_get_valid, a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_const_infop p1 = (png_const_infop)(uintptr_t)OV_U64(a);
  png_uint_32 p2 = (png_uint_32)OV_U64(a);
  png_uint_32 r = png_get_valid(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(png_get_rowbytes)(rc_ctx *c) {
  OV_ARGS_OF(png_get_rowbytes, a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_const_infop p1 = (png_const_infop)(uintptr_t)OV_U64(a);
  png_size_t r = png_get_rowbytes(p0, p1);
  OV_RET_U64(c, r);
}

void RC_FN(png_get_gAMA)(rc_ctx *c) {
  OV_ARGS_OF(png_get_gAMA, a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_const_infop p1 = (png_const_infop)(uintptr_t)OV_U64(a);
  double * p2 = (double *)(uintptr_t)OV_U64(a);
  png_uint_32 r = png_get_gAMA(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(png_get_IHDR)(rc_ctx *c) {
  OV_ARGS_OF(png_get_IHDR, a, c);
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

void RC_FN(png_create_read_struct)(rc_ctx *c) {
  OV_ARGS_OF(png_create_read_struct, a, c);
  png_const_charp p0 = (png_const_charp)(uintptr_t)OV_U64(a);
  png_voidp p1 = (png_voidp)(uintptr_t)OV_U64(a);
  png_error_ptr p2 = OV_FN(png_error_ptr, OV_U64(a));
  png_error_ptr p3 = OV_FN(png_error_ptr, OV_U64(a));
  png_structp r = png_create_read_struct(p0, p1, p2, p3);
  OV_RET_PTR(c, r);
}

void RC_FN(png_destroy_read_struct)(rc_ctx *c) {
  OV_ARGS_OF(png_destroy_read_struct, a, c);
  png_structpp p0 = (png_structpp)(uintptr_t)OV_U64(a);
  png_infopp p1 = (png_infopp)(uintptr_t)OV_U64(a);
  png_infopp p2 = (png_infopp)(uintptr_t)OV_U64(a);
  png_destroy_read_struct(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(png_read_end)(rc_ctx *c) {
  OV_ARGS_OF(png_read_end, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_read_end(p0, p1);
  ov_ret_void(c);
}

void RC_FN(png_read_row)(rc_ctx *c) {
  OV_ARGS_OF(png_read_row, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_bytep p1 = (png_bytep)(uintptr_t)OV_U64(a);
  png_bytep p2 = (png_bytep)(uintptr_t)OV_U64(a);
  png_read_row(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(png_read_image)(rc_ctx *c) {
  OV_ARGS_OF(png_read_image, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_bytepp p1 = (png_bytepp)(uintptr_t)OV_U64(a);
  png_read_image(p0, p1);
  ov_ret_void(c);
}

void RC_FN(png_read_update_info)(rc_ctx *c) {
  OV_ARGS_OF(png_read_update_info, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_read_update_info(p0, p1);
  ov_ret_void(c);
}

void RC_FN(png_read_info)(rc_ctx *c) {
  OV_ARGS_OF(png_read_info, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_infop p1 = (png_infop)(uintptr_t)OV_U64(a);
  png_read_info(p0, p1);
  ov_ret_void(c);
}

void RC_FN(png_set_read_fn)(rc_ctx *c) {
  OV_ARGS_OF(png_set_read_fn, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_voidp p1 = (png_voidp)(uintptr_t)OV_U64(a);
  png_rw_ptr p2 = OV_FN(png_rw_ptr, OV_U64(a));
  png_set_read_fn(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(png_set_strip_16)(rc_ctx *c) {
  OV_ARGS_OF(png_set_strip_16, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_strip_16(p0);
  ov_ret_void(c);
}

void RC_FN(png_set_palette_to_rgb)(rc_ctx *c) {
  OV_ARGS_OF(png_set_palette_to_rgb, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_palette_to_rgb(p0);
  ov_ret_void(c);
}

void RC_FN(png_set_expand_gray_1_2_4_to_8)(rc_ctx *c) {
  OV_ARGS_OF(png_set_expand_gray_1_2_4_to_8, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_expand_gray_1_2_4_to_8(p0);
  ov_ret_void(c);
}

void RC_FN(png_set_tRNS_to_alpha)(rc_ctx *c) {
  OV_ARGS_OF(png_set_tRNS_to_alpha, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_tRNS_to_alpha(p0);
  ov_ret_void(c);
}

void RC_FN(png_set_gray_to_rgb)(rc_ctx *c) {
  OV_ARGS_OF(png_set_gray_to_rgb, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_set_gray_to_rgb(p0);
  ov_ret_void(c);
}

void RC_FN(png_set_gamma)(rc_ctx *c) {
  OV_ARGS_OF(png_set_gamma, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  double p1 = OV_F64(a);
  double p2 = OV_F64(a);
  png_set_gamma(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(png_error)(rc_ctx *c) {
  OV_ARGS_OF(png_error, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  png_const_charp p1 = (png_const_charp)(uintptr_t)OV_U64(a);
  png_error(p0, p1);
  ov_ret_void(c);
}

void RC_FN(png_get_error_ptr)(rc_ctx *c) {
  OV_ARGS_OF(png_get_error_ptr, a, c);
  png_const_structp p0 = (png_const_structp)(uintptr_t)OV_U64(a);
  png_voidp r = png_get_error_ptr(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(png_longjmp)(rc_ctx *c) {
  OV_ARGS_OF(png_longjmp, a, c);
  png_structp p0 = (png_structp)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  png_longjmp(p0, p1);
  ov_ret_void(c);
}
