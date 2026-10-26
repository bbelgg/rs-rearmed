// Scaleform's JPEG loader writes guest function pointers into the error manager and the source
// manager of a jpeg_decompress_struct; rs_jpeg8 calls them natively, so they are replaced by
// native-callable pointers (a trampoline, or the native function itself for jpeg_resync_to_restart,
// which the loader installs as the default src->resync_to_restart).
#ifndef OV_JPEG_GLUE_H
#define OV_JPEG_GLUE_H
#include <stdio.h>

#include "jpeglib.h"
#include "rc_ov.h"

// The loader stores &jpeg_resync_to_restart (0x100b44270) as src->resync_to_restart: the guest address
// maps to the native function itself (no trampoline round trip into the adapter).
static inline void ov_jpeg_alias(void) {
  static int done;
  if (!done) {
    ov_alias_add(0x100b44270, (void *)jpeg_resync_to_restart);
    done = 1;
  }
}

static inline void ov_jpeg_fix_err(j_decompress_ptr ci) {
  ov_jpeg_alias();
  struct jpeg_error_mgr *e = ci->err;
  if (!e) return;
  ov_fix_fn(&e->error_exit);
  ov_fix_fn(&e->emit_message);
  ov_fix_fn(&e->output_message);
  ov_fix_fn(&e->format_message);
  ov_fix_fn(&e->reset_error_mgr);
}
static inline void ov_jpeg_fix(j_decompress_ptr ci) {
  ov_jpeg_fix_err(ci);
  struct jpeg_source_mgr *s = ci->src;
  if (s) {
    ov_fix_fn(&s->init_source);
    ov_fix_fn(&s->fill_input_buffer);
    ov_fix_fn(&s->skip_input_data);
    ov_fix_fn(&s->resync_to_restart);
    ov_fix_fn(&s->term_source);
  }
  if (ci->progress) ov_fix_fn(&ci->progress->progress_monitor);
}
#endif
