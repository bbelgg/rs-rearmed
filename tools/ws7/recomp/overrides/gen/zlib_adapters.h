// Adapter table of override module `zlib` (generated, checked in).
#ifndef OV_GEN_ZLIB_H
#define OV_GEN_ZLIB_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(compress2)(rc_ctx *c);
void RC_FN(compressBound)(rc_ctx *c);
void RC_FN(crc32)(rc_ctx *c);
void RC_FN(inflateReset)(rc_ctx *c);
void RC_FN(inflateInit_)(rc_ctx *c);
void RC_FN(inflate)(rc_ctx *c);
void RC_FN(inflateEnd)(rc_ctx *c);
void RC_FN(uncompress)(rc_ctx *c);
static const ov_entry ov_zlib_entries[] = {
  {RC_ADDR(compress2), "compress2", RC_FN(compress2)},
  {RC_ADDR(compressBound), "compressBound", RC_FN(compressBound)},
  {RC_ADDR(crc32), "crc32", RC_FN(crc32)},
  {RC_ADDR(inflateReset), "inflateReset", RC_FN(inflateReset)},
  {RC_ADDR(inflateInit_), "inflateInit_", RC_FN(inflateInit_)},
  {RC_ADDR(inflate), "inflate", RC_FN(inflate)},
  {RC_ADDR(inflateEnd), "inflateEnd", RC_FN(inflateEnd)},
  {RC_ADDR(uncompress), "uncompress", RC_FN(uncompress)},
};
#define OV_ZLIB_COUNT 8
#endif
