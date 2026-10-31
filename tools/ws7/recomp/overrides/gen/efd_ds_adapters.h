// Adapter table of override module `efd_ds` (generated, checked in).
#ifndef OV_GEN_EFD_DS_H
#define OV_GEN_EFD_DS_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(rsg_efd_datastream_init)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_init_copy)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_destroy)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_read)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_write)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_seek)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_write_stream)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_read_stream)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_write_id)(rc_ctx *c);
void RC_FN(rsg_efd_datastream_read_id)(rc_ctx *c);
static const ov_entry ov_efd_ds_entries[] = {
  {RC_ADDR(rsg_efd_datastream_init), "rsg_efd_datastream_init", RC_FN(rsg_efd_datastream_init)},
  {RC_ADDR(rsg_efd_datastream_init_copy), "rsg_efd_datastream_init_copy", RC_FN(rsg_efd_datastream_init_copy)},
  {RC_ADDR(rsg_efd_datastream_destroy), "rsg_efd_datastream_destroy", RC_FN(rsg_efd_datastream_destroy)},
  {RC_ADDR(rsg_efd_datastream_read), "rsg_efd_datastream_read", RC_FN(rsg_efd_datastream_read)},
  {RC_ADDR(rsg_efd_datastream_write), "rsg_efd_datastream_write", RC_FN(rsg_efd_datastream_write)},
  {RC_ADDR(rsg_efd_datastream_seek), "rsg_efd_datastream_seek", RC_FN(rsg_efd_datastream_seek)},
  {RC_ADDR(rsg_efd_datastream_write_stream), "rsg_efd_datastream_write_stream", RC_FN(rsg_efd_datastream_write_stream)},
  {RC_ADDR(rsg_efd_datastream_read_stream), "rsg_efd_datastream_read_stream", RC_FN(rsg_efd_datastream_read_stream)},
  {RC_ADDR(rsg_efd_datastream_write_id), "rsg_efd_datastream_write_id", RC_FN(rsg_efd_datastream_write_id)},
  {RC_ADDR(rsg_efd_datastream_read_id), "rsg_efd_datastream_read_id", RC_FN(rsg_efd_datastream_read_id)},
};
#define OV_EFD_DS_COUNT 10
#endif
