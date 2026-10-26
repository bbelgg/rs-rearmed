// libpng adapters that are not a plain translation.
#include <setjmp.h>

#include "png.h"
#include "rc_ov.h"

/* 0x100b67500 png_set_longjmp_fn(png_ptr, longjmp_fn, jmp_buf_size)
 * The loader passes the C library's longjmp (a bound import) and sizeof(jmp_buf) of x86_64 macOS
 * (148). The jmp_buf it later fills with setjmp belongs to the translated code (rc_setjmp.c), so
 * the C library's longjmp must not be used on it: ov_longjmp unwinds through rc_longjmp. The
 * native jmp_buf is larger than 148 bytes; the guest only needs the address of the buffer. */
void F_100b67500(rc_ctx *c) {
  OV_ARGS(a, c);
  png_structp png = OV_PTR(a, struct png_struct_def);
  uint64_t fn = OV_U64(a);
  size_t size = (size_t)OV_U64(a);
  png_longjmp_ptr f = ov_is_libc_longjmp((void *)(uintptr_t)fn) ? (png_longjmp_ptr)ov_longjmp
                                                                : OV_FN(png_longjmp_ptr, fn);
  jmp_buf *r = png_set_longjmp_fn(png, f, size == 148 ? sizeof(jmp_buf) : size);
  OV_RET_PTR(c, r);
}
