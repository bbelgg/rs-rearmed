// Adapter table of override module `jpeg` (generated, checked in).
#ifndef OV_GEN_JPEG_H
#define OV_GEN_JPEG_H
#include "rc.h"
#include "rc_ov.h"
void F_100b40bf0(rc_ctx *c);
void F_100b41030(rc_ctx *c);
void F_100b41100(rc_ctx *c);
void F_100b41180(rc_ctx *c);
void F_100b41190(rc_ctx *c);
void F_100b411a0(rc_ctx *c);
void F_100b413b0(rc_ctx *c);
void F_100b42800(rc_ctx *c);
void F_100b44270(rc_ctx *c);
void F_100b57920(rc_ctx *c);
static const ov_entry ov_jpeg_entries[] = {
  {0x100b40bf0ull, "jpeg_CreateDecompress", F_100b40bf0},
  {0x100b41030ull, "jpeg_finish_decompress", F_100b41030},
  {0x100b41100ull, "jpeg_read_header", F_100b41100},
  {0x100b41180ull, "jpeg_abort_decompress", F_100b41180},
  {0x100b41190ull, "jpeg_destroy_decompress", F_100b41190},
  {0x100b411a0ull, "jpeg_start_decompress", F_100b411a0},
  {0x100b413b0ull, "jpeg_read_scanlines", F_100b413b0},
  {0x100b42800ull, "jpeg_read_coefficients", F_100b42800},
  {0x100b44270ull, "jpeg_resync_to_restart", F_100b44270},
  {0x100b57920ull, "jpeg_std_error", F_100b57920},
};
#define OV_JPEG_COUNT 10
#endif
