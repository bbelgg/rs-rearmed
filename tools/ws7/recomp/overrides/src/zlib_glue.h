// z_stream's allocator callbacks may be guest functions (Scaleform's SWF loader); system libz
// calls them natively, so they become trampolines (a NULL zalloc/zfree stays NULL: libz mallocs).
#ifndef OV_ZLIB_GLUE_H
#define OV_ZLIB_GLUE_H
#include <zlib.h>

#include "rc_ov.h"

static inline void ov_zlib_fix(z_streamp s) {
  if (!s) return;
  ov_fix_fn(&s->zalloc);
  ov_fix_fn(&s->zfree);
}
#endif
