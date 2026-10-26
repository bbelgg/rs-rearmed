// Adapters of override module `online` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_online.h"

/* 0x100a24d90 dlc_worker_flush: returns 0 without doing anything */
void F_100a24d90(rc_ctx *c) { ov_ret_u64(c, 0); }

/* 0x100a2a530 open_uplay_url: returns 0 without doing anything */
void F_100a2a530(rc_ctx *c) { ov_ret_u64(c, 0); }

/* 0x100a2c720 dlc_connection_changed: returns 0 without doing anything */
void F_100a2c720(rc_ctx *c) { ov_ret_u64(c, 0); }

/* 0x101315d70 autologin: returns 0 without doing anything */
void F_101315d70(rc_ctx *c) { ov_ret_u64(c, 0); }
