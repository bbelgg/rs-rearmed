// Game addresses by name, for the tests that run or read the original image (shims/game/harness,
// shims/game/test). The RS_ORIG_*, RS_KEY_* and RS_RANGE_* defines of rs_game.h, rs_crypto.h,
// rs_online.h and the harness expand to RC_ADDR(name): the unslid address of that name in the target's
// tables (tools/targets/<target>/symbols.map), from the rc_target_names.h that
// `tools/targets/targets.py gen` writes (cmake/RSTargetNames.cmake). The harness maps the Mac image, so
// its build uses the mac-x64 tables. Code built into the app gets the same macro from
// tools/ws7/recomp/runtime/rc_abi.h.
#ifndef RS_NAMES_H
#define RS_NAMES_H

#include "rc_target_names.h"

#ifndef RC_ADDR
#define RC_ADDR(name) RC_ADDR_##name
#endif

#endif
