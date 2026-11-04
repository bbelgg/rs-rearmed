// 32-bit guests: import calls a packer replaced whose import the wrapper table could not name
// (xlat/x86_32.py set_wrappers, build/gen_image_pe.py --wrappers). The translation does not run
// the packer's stub; such a site calls here instead, so a run that reaches one says where.
#include <stdio.h>

#include "rc32.h"

void rc32_wrapper_unresolved(rc_ctx *c, uint32_t site, uint32_t stub) {
  (void)c;
  fprintf(stderr, "rc32: import call at 0x%x through packer stub 0x%x: import not resolved\n", site, stub);
  rc_fail("unresolved packer import", site);
}
