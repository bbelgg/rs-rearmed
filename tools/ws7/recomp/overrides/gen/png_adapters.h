// Adapter table of override module `png` (generated, checked in).
#ifndef OV_GEN_PNG_H
#define OV_GEN_PNG_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(png_set_sig_bytes)(rc_ctx *c);
void RC_FN(png_get_io_ptr)(rc_ctx *c);
void RC_FN(png_sig_cmp)(rc_ctx *c);
void RC_FN(png_create_info_struct)(rc_ctx *c);
void RC_FN(png_get_valid)(rc_ctx *c);
void RC_FN(png_get_rowbytes)(rc_ctx *c);
void RC_FN(png_get_gAMA)(rc_ctx *c);
void RC_FN(png_get_IHDR)(rc_ctx *c);
void RC_FN(png_create_read_struct)(rc_ctx *c);
void RC_FN(png_destroy_read_struct)(rc_ctx *c);
void RC_FN(png_read_end)(rc_ctx *c);
void RC_FN(png_read_row)(rc_ctx *c);
void RC_FN(png_read_image)(rc_ctx *c);
void RC_FN(png_read_update_info)(rc_ctx *c);
void RC_FN(png_read_info)(rc_ctx *c);
void RC_FN(png_set_read_fn)(rc_ctx *c);
void RC_FN(png_set_strip_16)(rc_ctx *c);
void RC_FN(png_set_palette_to_rgb)(rc_ctx *c);
void RC_FN(png_set_expand_gray_1_2_4_to_8)(rc_ctx *c);
void RC_FN(png_set_tRNS_to_alpha)(rc_ctx *c);
void RC_FN(png_set_gray_to_rgb)(rc_ctx *c);
void RC_FN(png_set_gamma)(rc_ctx *c);
void RC_FN(png_error)(rc_ctx *c);
void RC_FN(png_set_longjmp_fn)(rc_ctx *c);
void RC_FN(png_get_error_ptr)(rc_ctx *c);
void RC_FN(png_longjmp)(rc_ctx *c);
static const ov_entry ov_png_entries[] = {
  {RC_ADDR(png_set_sig_bytes), "png_set_sig_bytes", RC_FN(png_set_sig_bytes)},
  {RC_ADDR(png_get_io_ptr), "png_get_io_ptr", RC_FN(png_get_io_ptr)},
  {RC_ADDR(png_sig_cmp), "png_sig_cmp", RC_FN(png_sig_cmp)},
  {RC_ADDR(png_create_info_struct), "png_create_info_struct", RC_FN(png_create_info_struct)},
  {RC_ADDR(png_get_valid), "png_get_valid", RC_FN(png_get_valid)},
  {RC_ADDR(png_get_rowbytes), "png_get_rowbytes", RC_FN(png_get_rowbytes)},
  {RC_ADDR(png_get_gAMA), "png_get_gAMA", RC_FN(png_get_gAMA)},
  {RC_ADDR(png_get_IHDR), "png_get_IHDR", RC_FN(png_get_IHDR)},
  {RC_ADDR(png_create_read_struct), "png_create_read_struct", RC_FN(png_create_read_struct)},
  {RC_ADDR(png_destroy_read_struct), "png_destroy_read_struct", RC_FN(png_destroy_read_struct)},
  {RC_ADDR(png_read_end), "png_read_end", RC_FN(png_read_end)},
  {RC_ADDR(png_read_row), "png_read_row", RC_FN(png_read_row)},
  {RC_ADDR(png_read_image), "png_read_image", RC_FN(png_read_image)},
  {RC_ADDR(png_read_update_info), "png_read_update_info", RC_FN(png_read_update_info)},
  {RC_ADDR(png_read_info), "png_read_info", RC_FN(png_read_info)},
  {RC_ADDR(png_set_read_fn), "png_set_read_fn", RC_FN(png_set_read_fn)},
  {RC_ADDR(png_set_strip_16), "png_set_strip_16", RC_FN(png_set_strip_16)},
  {RC_ADDR(png_set_palette_to_rgb), "png_set_palette_to_rgb", RC_FN(png_set_palette_to_rgb)},
  {RC_ADDR(png_set_expand_gray_1_2_4_to_8), "png_set_expand_gray_1_2_4_to_8", RC_FN(png_set_expand_gray_1_2_4_to_8)},
  {RC_ADDR(png_set_tRNS_to_alpha), "png_set_tRNS_to_alpha", RC_FN(png_set_tRNS_to_alpha)},
  {RC_ADDR(png_set_gray_to_rgb), "png_set_gray_to_rgb", RC_FN(png_set_gray_to_rgb)},
  {RC_ADDR(png_set_gamma), "png_set_gamma", RC_FN(png_set_gamma)},
  {RC_ADDR(png_error), "png_error", RC_FN(png_error)},
  {RC_ADDR(png_set_longjmp_fn), "png_set_longjmp_fn", RC_FN(png_set_longjmp_fn)},
  {RC_ADDR(png_get_error_ptr), "png_get_error_ptr", RC_FN(png_get_error_ptr)},
  {RC_ADDR(png_longjmp), "png_longjmp", RC_FN(png_longjmp)},
};
#define OV_PNG_COUNT 26
#endif
