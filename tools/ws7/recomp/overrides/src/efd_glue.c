// efd adapters that are not a plain translation.
#include <stdarg.h>

#include "rc_ov.h"
#include "rs_efd.h"

/* 0x10130b200 efd::Snprintf(char *buf, size_t count, const char *fmt, ...): variadic. The guest's
 * arguments (registers, then stack) become an arm64 va_list for rsg_efd_vsnprintf. */
void F_10130b200(rc_ctx *c) {
  OV_ARGS(a, c);
  char *buf = OV_PTR(a, char);
  size_t count = (size_t)OV_U64(a);
  const char *fmt = OV_PTR(a, const char);
  uint64_t slots[rc_va_max(fmt)];
  void *vl = rc_va_from_regs(&a, fmt, RC_VA_PRINTF, slots);
  va_list ap;
  memcpy(&ap, &vl, sizeof ap);
  int r = rsg_efd_vsnprintf(buf, count, fmt, ap);
  OV_RET_I32(c, r);
}

/* 0x1012f8d60 the fast-sqrt table: the original allocates 0x400 bytes with the efd allocator
 * (0x1012eff00(size, hint)), fills them and returns the pointer; the rewrite fills a buffer. */
void F_1012f8d60(rc_ctx *c) {
  uint32_t *t = (uint32_t *)(uintptr_t)OV_GUEST(0x1012eff00, 0x400, 0);
  rsg_efd_fill_sqrt_table(t);
  ov_ret_u64(c, (uint64_t)(uintptr_t)t);
}
