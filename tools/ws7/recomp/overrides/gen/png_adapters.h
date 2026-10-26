// Adapter table of override module `png` (generated, checked in).
#ifndef OV_GEN_PNG_H
#define OV_GEN_PNG_H
#include "rc.h"
#include "rc_ov.h"
void F_100b58c70(rc_ctx *c);
void F_100b58ca0(rc_ctx *c);
void F_100b5aea0(rc_ctx *c);
void F_100b5b170(rc_ctx *c);
void F_100b5c4a0(rc_ctx *c);
void F_100b5c4c0(rc_ctx *c);
void F_100b5c4e0(rc_ctx *c);
void F_100b5c510(rc_ctx *c);
void F_100b61590(rc_ctx *c);
void F_100b61780(rc_ctx *c);
void F_100b61880(rc_ctx *c);
void F_100b61c90(rc_ctx *c);
void F_100b62170(rc_ctx *c);
void F_100b62240(rc_ctx *c);
void F_100b62270(rc_ctx *c);
void F_100b62970(rc_ctx *c);
void F_100b62a30(rc_ctx *c);
void F_100b62a50(rc_ctx *c);
void F_100b62a70(rc_ctx *c);
void F_100b62a90(rc_ctx *c);
void F_100b62ab0(rc_ctx *c);
void F_100b66cb0(rc_ctx *c);
void F_100b67080(rc_ctx *c);
void F_100b67500(rc_ctx *c);
void F_100b67550(rc_ctx *c);
void F_100b67570(rc_ctx *c);
static const ov_entry ov_png_entries[] = {
  {0x100b58c70ull, "png_set_sig_bytes", F_100b58c70},
  {0x100b58ca0ull, "png_get_io_ptr", F_100b58ca0},
  {0x100b5aea0ull, "png_sig_cmp", F_100b5aea0},
  {0x100b5b170ull, "png_create_info_struct", F_100b5b170},
  {0x100b5c4a0ull, "png_get_valid", F_100b5c4a0},
  {0x100b5c4c0ull, "png_get_rowbytes", F_100b5c4c0},
  {0x100b5c4e0ull, "png_get_gAMA", F_100b5c4e0},
  {0x100b5c510ull, "png_get_IHDR", F_100b5c510},
  {0x100b61590ull, "png_create_read_struct", F_100b61590},
  {0x100b61780ull, "png_destroy_read_struct", F_100b61780},
  {0x100b61880ull, "png_read_end", F_100b61880},
  {0x100b61c90ull, "png_read_row", F_100b61c90},
  {0x100b62170ull, "png_read_image", F_100b62170},
  {0x100b62240ull, "png_read_update_info", F_100b62240},
  {0x100b62270ull, "png_read_info", F_100b62270},
  {0x100b62970ull, "png_set_read_fn", F_100b62970},
  {0x100b62a30ull, "png_set_strip_16", F_100b62a30},
  {0x100b62a50ull, "png_set_palette_to_rgb", F_100b62a50},
  {0x100b62a70ull, "png_set_expand_gray_1_2_4_to_8", F_100b62a70},
  {0x100b62a90ull, "png_set_tRNS_to_alpha", F_100b62a90},
  {0x100b62ab0ull, "png_set_gray_to_rgb", F_100b62ab0},
  {0x100b66cb0ull, "png_set_gamma", F_100b66cb0},
  {0x100b67080ull, "png_error", F_100b67080},
  {0x100b67500ull, "png_set_longjmp_fn", F_100b67500},
  {0x100b67550ull, "png_get_error_ptr", F_100b67550},
  {0x100b67570ull, "png_longjmp", F_100b67570},
};
#define OV_PNG_COUNT 26
#endif
