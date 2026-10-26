// The rebuilt libpng (third_party/libpng-1.5.13) behind the png_api of png_cases.h.
#ifndef RS_GAME_PNG_REWRITE_H
#define RS_GAME_PNG_REWRITE_H

#include "png_cases.h"

static const png_api png_rewrite_api = {
    .create_read_struct = png_create_read_struct,
    .create_info_struct = png_create_info_struct,
    .destroy_read_struct = png_destroy_read_struct,
    .set_read_fn = png_set_read_fn,
    .set_sig_bytes = png_set_sig_bytes,
    .sig_cmp = png_sig_cmp,
    .read_info = png_read_info,
    .get_IHDR = png_get_IHDR,
    .get_valid = png_get_valid,
    .get_gAMA = png_get_gAMA,
    .get_rowbytes = png_get_rowbytes,
    .set_strip_16 = png_set_strip_16,
    .set_palette_to_rgb = png_set_palette_to_rgb,
    .set_expand_gray_1_2_4_to_8 = png_set_expand_gray_1_2_4_to_8,
    .set_tRNS_to_alpha = png_set_tRNS_to_alpha,
    .set_gray_to_rgb = png_set_gray_to_rgb,
    .set_gamma = png_set_gamma,
    .read_update_info = png_read_update_info,
    .read_image = png_read_image,
    .read_row = png_read_row,
    .read_end = png_read_end,
};

#endif
