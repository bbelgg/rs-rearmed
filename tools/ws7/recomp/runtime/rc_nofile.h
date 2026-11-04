// The open-file limit (rsmacfixes' fix 4), for both editions: an app started from Finder gets launchd's soft
// limit of 256, and every file the game has open is one host descriptor. Mac: overrides/mods/mods.c; Learn &
// Play: win-x86/shims/win32_overrides.c.
#ifndef RC_NOFILE_H
#define RC_NOFILE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

// Raises the soft limit to 10240, or the most the system allows, and never lowers it. Returns the limit now and
// says in *before what it was; *failed is set when it should have gone up and setrlimit refused.
static inline uint64_t rc_raise_nofile(uint64_t *before, int *failed) {
  struct rlimit rl;
  *failed = 0;
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return *before = 0;
  rlim_t old = rl.rlim_cur, want = 10240;
  if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want) want = rl.rlim_max;
  int maxproc = 0;
  size_t n = sizeof maxproc;
  if (sysctlbyname("kern.maxfilesperproc", &maxproc, &n, NULL, 0) == 0 && maxproc > 0 && (rlim_t)maxproc < want)
    want = (rlim_t)maxproc;
  *before = old;
  if (want > old) {
    rl.rlim_cur = want;
    *failed = setrlimit(RLIMIT_NOFILE, &rl) != 0;
    getrlimit(RLIMIT_NOFILE, &rl);
  }
  return rl.rlim_cur;
}

#endif
