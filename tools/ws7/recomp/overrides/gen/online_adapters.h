// Adapter table of override module `online` (generated, checked in).
#ifndef OV_GEN_ONLINE_H
#define OV_GEN_ONLINE_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(efd_get_config_bool)(rc_ctx *c);
void RC_FN(rsconnect_factory)(rc_ctx *c);
void RC_FN(signin_start_no_uplay)(rc_ctx *c);
void RC_FN(dlc_worker_flush)(rc_ctx *c);
void RC_FN(dlc_job_preview)(rc_ctx *c);
void RC_FN(dlc_job_request)(rc_ctx *c);
void RC_FN(open_uplay_url)(rc_ctx *c);
void RC_FN(dlc_connection_changed)(rc_ctx *c);
void RC_FN(autologin)(rc_ctx *c);
static const ov_entry ov_online_entries[] = {
  {RC_ADDR(efd_get_config_bool), "efd_get_config_bool", RC_FN(efd_get_config_bool)},
  {RC_ADDR(rsconnect_factory), "rsconnect_factory", RC_FN(rsconnect_factory)},
  {RC_ADDR(signin_start_no_uplay), "signin_start_no_uplay", RC_FN(signin_start_no_uplay)},
  {RC_ADDR(dlc_worker_flush), "dlc_worker_flush", RC_FN(dlc_worker_flush)},
  {RC_ADDR(dlc_job_preview), "dlc_job_preview", RC_FN(dlc_job_preview)},
  {RC_ADDR(dlc_job_request), "dlc_job_request", RC_FN(dlc_job_request)},
  {RC_ADDR(open_uplay_url), "open_uplay_url", RC_FN(open_uplay_url)},
  {RC_ADDR(dlc_connection_changed), "dlc_connection_changed", RC_FN(dlc_connection_changed)},
  {RC_ADDR(autologin), "autologin", RC_FN(autologin)},
};
#define OV_ONLINE_COUNT 9
#endif
