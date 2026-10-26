// Adapters of override module `zlib` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "zlib.h"
#include "zlib_glue.h"

/* 0x100e07620 compress2 */
void F_100e07620(rc_ctx *c) {
  OV_ARGS(a, c);
  Bytef * p0 = (Bytef *)(uintptr_t)OV_U64(a);
  uLongf * p1 = (uLongf *)(uintptr_t)OV_U64(a);
  const Bytef * p2 = (const Bytef *)(uintptr_t)OV_U64(a);
  uLong p3 = (uLong)OV_U64(a);
  int p4 = (int)OV_U64(a);
  int r = compress2(p0, p1, p2, p3, p4);
  OV_RET_I32(c, r);
}

/* 0x100e076d0 compressBound */
void F_100e076d0(rc_ctx *c) {
  OV_ARGS(a, c);
  uLong p0 = (uLong)OV_U64(a);
  uLong r = compressBound(p0);
  OV_RET_U64(c, r);
}

/* 0x100e076f0 crc32 */
void F_100e076f0(rc_ctx *c) {
  OV_ARGS(a, c);
  uLong p0 = (uLong)OV_U64(a);
  const Bytef * p1 = (const Bytef *)(uintptr_t)OV_U64(a);
  uInt p2 = (uInt)OV_U64(a);
  uLong r = crc32(p0, p1, p2);
  OV_RET_U64(c, r);
}

/* 0x100e0b070 inflateReset */
void F_100e0b070(rc_ctx *c) {
  OV_ARGS(a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  ov_zlib_fix(p0);
  int r = inflateReset(p0);
  OV_RET_I32(c, r);
}

/* 0x100e0b120 inflateInit_ */
void F_100e0b120(rc_ctx *c) {
  OV_ARGS(a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  ov_zlib_fix(p0);
  int r = inflateInit_(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100e0b260 inflate */
void F_100e0b260(rc_ctx *c) {
  OV_ARGS(a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  ov_zlib_fix(p0);
  int r = inflate(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e0cf20 inflateEnd */
void F_100e0cf20(rc_ctx *c) {
  OV_ARGS(a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  int r = inflateEnd(p0);
  OV_RET_I32(c, r);
}

/* 0x100e0fa10 uncompress */
void F_100e0fa10(rc_ctx *c) {
  OV_ARGS(a, c);
  Bytef * p0 = (Bytef *)(uintptr_t)OV_U64(a);
  uLongf * p1 = (uLongf *)(uintptr_t)OV_U64(a);
  const Bytef * p2 = (const Bytef *)(uintptr_t)OV_U64(a);
  uLong p3 = (uLong)OV_U64(a);
  int r = uncompress(p0, p1, p2, p3);
  OV_RET_I32(c, r);
}
