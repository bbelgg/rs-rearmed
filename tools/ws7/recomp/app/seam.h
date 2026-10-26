// The platform seam of the arm64 app.
//
// The arm64 executable's main() is the native rewrite of the shell
// (shims/platform: AppDelegate/OpenGLView/MainController, the input queue,
// paths, Rocksmith.ini). The engine behind it is the translated x86 image.
// seam_platform.c is the engine side of rsp_engine_hooks: each hook is one
// (or a few) rc_call_guest calls into translated functions. Addresses are
// the unslid vmaddrs of the original image.
#ifndef RCAPP_SEAM_H
#define RCAPP_SEAM_H

#include "rsp_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// The hook table for rsp_set_hooks(). Valid for the life of the process.
const rsp_engine_hooks *rcapp_seam_hooks(void);

// 1 once the translated GameBootstrap has completed successfully.
int rcapp_seam_booted(void);

// Call the translated function at image address `vmaddr` (unslid) with up to
// 6+ integer arguments, on the calling thread's guest context and, off the
// main thread, on a large native stack (translated C functions nest as C
// frames; a CVDisplayLink thread has only 512 KB).
uint64_t rcapp_gcall(uint64_t vmaddr, int nargs, const uint64_t *args);

// Run fn(arg) on a large native stack unless this is the main thread or the
// stack is already switched. RC_NATIVE_STACK_MB (default 256, 0 = never).
void rcapp_on_big_stack(void (*fn)(void *), void *arg);

#ifdef __cplusplus
}
#endif
#endif
