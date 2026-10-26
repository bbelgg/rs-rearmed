// See efd_env.h.
#include "efd_env.h"

#include <stdint.h>

#include "rsimage.h"

int efd_env_start(void) {
  int n = rsimage_run_inits(RS_RANGE_ENGINE_EFD);
  if (n < 0) return -1;
  RS_FN(void (*)(void), RS_ORIG_EFD_INIT)();
  return n;
}

void efd_env_set_logger(void *ilogger) {
  static void *holder[2];
  holder[1] = ilogger;
  *RS_FN(void ***, RS_ORIG_EFD_LOGGER_SINGLETON) = holder;
}
