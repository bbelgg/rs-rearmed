// The rebuilt libjpeg (third_party/jpeg-8d) behind the jpeg_api of jpeg_cases.h.
#ifndef RS_GAME_JPEG_REWRITE_H
#define RS_GAME_JPEG_REWRITE_H

#include "jpeg_cases.h"

static const jpeg_api jpeg_rewrite_api = {
    .std_error = jpeg_std_error,
    .create_decompress = jpeg_CreateDecompress,
    .read_header = jpeg_read_header,
    .start_decompress = jpeg_start_decompress,
    .read_scanlines = jpeg_read_scanlines,
    .finish_decompress = jpeg_finish_decompress,
    .destroy_decompress = jpeg_destroy_decompress,
    .read_coefficients = jpeg_read_coefficients,
    .resync_to_restart = jpeg_resync_to_restart,
};

#endif
