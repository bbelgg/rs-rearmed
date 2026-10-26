// Adapter table of override module `zlib` (generated, checked in).
#ifndef OV_GEN_ZLIB_H
#define OV_GEN_ZLIB_H
#include "rc.h"
#include "rc_ov.h"
void F_100e07620(rc_ctx *c);
void F_100e076d0(rc_ctx *c);
void F_100e076f0(rc_ctx *c);
void F_100e0b070(rc_ctx *c);
void F_100e0b120(rc_ctx *c);
void F_100e0b260(rc_ctx *c);
void F_100e0cf20(rc_ctx *c);
void F_100e0fa10(rc_ctx *c);
static const ov_entry ov_zlib_entries[] = {
  {0x100e07620ull, "compress2", F_100e07620},
  {0x100e076d0ull, "compressBound", F_100e076d0},
  {0x100e076f0ull, "crc32", F_100e076f0},
  {0x100e0b070ull, "inflateReset", F_100e0b070},
  {0x100e0b120ull, "inflateInit_", F_100e0b120},
  {0x100e0b260ull, "inflate", F_100e0b260},
  {0x100e0cf20ull, "inflateEnd", F_100e0cf20},
  {0x100e0fa10ull, "uncompress", F_100e0fa10},
};
#define OV_ZLIB_COUNT 8
#endif
