// Adapter table of override module `jpeg` (generated, checked in).
#ifndef OV_GEN_JPEG_H
#define OV_GEN_JPEG_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(jpeg_CreateDecompress)(rc_ctx *c);
void RC_FN(jpeg_finish_decompress)(rc_ctx *c);
void RC_FN(jpeg_read_header)(rc_ctx *c);
void RC_FN(jpeg_abort_decompress)(rc_ctx *c);
void RC_FN(jpeg_destroy_decompress)(rc_ctx *c);
void RC_FN(jpeg_start_decompress)(rc_ctx *c);
void RC_FN(jpeg_read_scanlines)(rc_ctx *c);
void RC_FN(jpeg_read_coefficients)(rc_ctx *c);
void RC_FN(jpeg_resync_to_restart)(rc_ctx *c);
void RC_FN(jpeg_std_error)(rc_ctx *c);
static const ov_entry ov_jpeg_entries[] = {
  {RC_ADDR(jpeg_CreateDecompress), "jpeg_CreateDecompress", RC_FN(jpeg_CreateDecompress)},
  {RC_ADDR(jpeg_finish_decompress), "jpeg_finish_decompress", RC_FN(jpeg_finish_decompress)},
  {RC_ADDR(jpeg_read_header), "jpeg_read_header", RC_FN(jpeg_read_header)},
  {RC_ADDR(jpeg_abort_decompress), "jpeg_abort_decompress", RC_FN(jpeg_abort_decompress)},
  {RC_ADDR(jpeg_destroy_decompress), "jpeg_destroy_decompress", RC_FN(jpeg_destroy_decompress)},
  {RC_ADDR(jpeg_start_decompress), "jpeg_start_decompress", RC_FN(jpeg_start_decompress)},
  {RC_ADDR(jpeg_read_scanlines), "jpeg_read_scanlines", RC_FN(jpeg_read_scanlines)},
  {RC_ADDR(jpeg_read_coefficients), "jpeg_read_coefficients", RC_FN(jpeg_read_coefficients)},
  {RC_ADDR(jpeg_resync_to_restart), "jpeg_resync_to_restart", RC_FN(jpeg_resync_to_restart)},
  {RC_ADDR(jpeg_std_error), "jpeg_std_error", RC_FN(jpeg_std_error)},
};
#define OV_JPEG_COUNT 10
#endif
