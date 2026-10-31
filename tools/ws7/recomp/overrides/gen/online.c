// Adapters of override module `online` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_online.h"

/* dlc_worker_flush: returns 0 without doing anything */
void RC_FN(dlc_worker_flush)(rc_ctx *c) { ov_ret_u64(c, 0); }

/* open_uplay_url: returns 0 without doing anything */
void RC_FN(open_uplay_url)(rc_ctx *c) { ov_ret_u64(c, 0); }

/* dlc_connection_changed: returns 0 without doing anything */
void RC_FN(dlc_connection_changed)(rc_ctx *c) { ov_ret_u64(c, 0); }

/* autologin: returns 0 without doing anything */
void RC_FN(autologin)(rc_ctx *c) { ov_ret_u64(c, 0); }
