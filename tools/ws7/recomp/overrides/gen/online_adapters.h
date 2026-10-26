// Adapter table of override module `online` (generated, checked in).
#ifndef OV_GEN_ONLINE_H
#define OV_GEN_ONLINE_H
#include "rc.h"
#include "rc_ov.h"
void F_100373cd0(rc_ctx *c);
void F_100a0c360(rc_ctx *c);
void F_100a13950(rc_ctx *c);
void F_100a24d90(rc_ctx *c);
void F_100a24e70(rc_ctx *c);
void F_100a25170(rc_ctx *c);
void F_100a2a530(rc_ctx *c);
void F_100a2c720(rc_ctx *c);
void F_101315d70(rc_ctx *c);
static const ov_entry ov_online_entries[] = {
  {0x100373cd0ull, "efd_get_config_bool", F_100373cd0},
  {0x100a0c360ull, "rsconnect_factory", F_100a0c360},
  {0x100a13950ull, "signin_start_no_uplay", F_100a13950},
  {0x100a24d90ull, "dlc_worker_flush", F_100a24d90},
  {0x100a24e70ull, "dlc_job_preview", F_100a24e70},
  {0x100a25170ull, "dlc_job_request", F_100a25170},
  {0x100a2a530ull, "open_uplay_url", F_100a2a530},
  {0x100a2c720ull, "dlc_connection_changed", F_100a2c720},
  {0x101315d70ull, "autologin", F_101315d70},
};
#define OV_ONLINE_COUNT 9
#endif
