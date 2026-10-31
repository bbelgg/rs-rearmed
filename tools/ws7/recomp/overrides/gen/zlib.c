// Adapters of override module `zlib` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "zlib.h"
#include "zlib_glue.h"

void RC_FN(compress2)(rc_ctx *c) {
  OV_ARGS_OF(compress2, a, c);
  Bytef * p0 = (Bytef *)(uintptr_t)OV_U64(a);
  uLongf * p1 = (uLongf *)(uintptr_t)OV_U64(a);
  const Bytef * p2 = (const Bytef *)(uintptr_t)OV_U64(a);
  uLong p3 = (uLong)OV_U64(a);
  int p4 = (int)OV_U64(a);
  int r = compress2(p0, p1, p2, p3, p4);
  OV_RET_I32(c, r);
}

void RC_FN(compressBound)(rc_ctx *c) {
  OV_ARGS_OF(compressBound, a, c);
  uLong p0 = (uLong)OV_U64(a);
  uLong r = compressBound(p0);
  OV_RET_U64(c, r);
}

void RC_FN(crc32)(rc_ctx *c) {
  OV_ARGS_OF(crc32, a, c);
  uLong p0 = (uLong)OV_U64(a);
  const Bytef * p1 = (const Bytef *)(uintptr_t)OV_U64(a);
  uInt p2 = (uInt)OV_U64(a);
  uLong r = crc32(p0, p1, p2);
  OV_RET_U64(c, r);
}

void RC_FN(inflateReset)(rc_ctx *c) {
  OV_ARGS_OF(inflateReset, a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  ov_zlib_fix(p0);
  int r = inflateReset(p0);
  OV_RET_I32(c, r);
}

void RC_FN(inflateInit_)(rc_ctx *c) {
  OV_ARGS_OF(inflateInit_, a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  ov_zlib_fix(p0);
  int r = inflateInit_(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(inflate)(rc_ctx *c) {
  OV_ARGS_OF(inflate, a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  ov_zlib_fix(p0);
  int r = inflate(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(inflateEnd)(rc_ctx *c) {
  OV_ARGS_OF(inflateEnd, a, c);
  z_streamp p0 = (z_streamp)(uintptr_t)OV_U64(a);
  int r = inflateEnd(p0);
  OV_RET_I32(c, r);
}

void RC_FN(uncompress)(rc_ctx *c) {
  OV_ARGS_OF(uncompress, a, c);
  Bytef * p0 = (Bytef *)(uintptr_t)OV_U64(a);
  uLongf * p1 = (uLongf *)(uintptr_t)OV_U64(a);
  const Bytef * p2 = (const Bytef *)(uintptr_t)OV_U64(a);
  uLong p3 = (uLong)OV_U64(a);
  int r = uncompress(p0, p1, p2, p3);
  OV_RET_I32(c, r);
}
