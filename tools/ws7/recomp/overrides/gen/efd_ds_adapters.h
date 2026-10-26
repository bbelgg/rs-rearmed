// Adapter table of override module `efd_ds` (generated, checked in).
#ifndef OV_GEN_EFD_DS_H
#define OV_GEN_EFD_DS_H
#include "rc.h"
#include "rc_ov.h"
void F_1012c1fe0(rc_ctx *c);
void F_1012c2080(rc_ctx *c);
void F_1012c2130(rc_ctx *c);
void F_1012c2200(rc_ctx *c);
void F_1012c2300(rc_ctx *c);
void F_1012c2460(rc_ctx *c);
void F_1012c2550(rc_ctx *c);
void F_1012c26b0(rc_ctx *c);
void F_1013036c0(rc_ctx *c);
void F_101303770(rc_ctx *c);
static const ov_entry ov_efd_ds_entries[] = {
  {0x1012c1fe0ull, "rsg_efd_datastream_init", F_1012c1fe0},
  {0x1012c2080ull, "rsg_efd_datastream_init_copy", F_1012c2080},
  {0x1012c2130ull, "rsg_efd_datastream_destroy", F_1012c2130},
  {0x1012c2200ull, "rsg_efd_datastream_read", F_1012c2200},
  {0x1012c2300ull, "rsg_efd_datastream_write", F_1012c2300},
  {0x1012c2460ull, "rsg_efd_datastream_seek", F_1012c2460},
  {0x1012c2550ull, "rsg_efd_datastream_write_stream", F_1012c2550},
  {0x1012c26b0ull, "rsg_efd_datastream_read_stream", F_1012c26b0},
  {0x1013036c0ull, "rsg_efd_datastream_write_id", F_1013036c0},
  {0x101303770ull, "rsg_efd_datastream_read_id", F_101303770},
};
#define OV_EFD_DS_COUNT 10
#endif
