// Shared by the efd rewrites (not installed).
#ifndef RS_EFD_INTERNAL_H
#define RS_EFD_INTERNAL_H

#include "rs_efd.h"

#ifdef __cplusplus
extern "C" {
#endif

void rsg_efd_logf(int module, int level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

// efd's error-level constants (0x10142cdf8...): module 3 is efd's own, the
// message helper logs as module 7.
enum { RSG_EFD_ERR0 = 0, RSG_EFD_ERR1 = 1, RSG_EFD_ERR2 = 2 };
enum { RSG_EFD_LOG_EFD = 3, RSG_EFD_LOG_MESSAGES = 7 };

// Source-file prefix of efd's log records: the tail of the originals' __FILE__
// (from "Emergent/" on). Log consumers match on the file name, so the build
// machine's checkout root is left out.
#define RSG_EFD_SRC "Emergent/Gamebryo/Source/Foundation/efd/"

#ifdef __cplusplus
}
#endif

#endif
