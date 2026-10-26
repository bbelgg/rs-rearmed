// Logging hook for the efd rewrites. The originals call
//   Logger *l = GetLogger();                       // 0x1012e5d10
//   if (l->IsLogging(module, level))               // slot 23 (*0xb8)
//     l->Log(module, level, __FILE__, __LINE__, Format(fmt, ...), &site);  // slot 11 (*0x58)
// with the message formatted by 0x1012e46a0 (vsnprintf into a 128-byte
// inline buffer, heap beyond). The rewrites format the same message and pass
// it to the hook.
#include <stdarg.h>
#include <stdio.h>

#include "efd_internal.h"

static rsg_efd_log_fn g_log;

void rsg_efd_set_log(rsg_efd_log_fn fn) { g_log = fn; }

void rsg_efd_logf(int module, int level, const char *file, int line, const char *fmt, ...) {
  if (!g_log) return;
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  g_log(module, level, file, line, msg);
}
