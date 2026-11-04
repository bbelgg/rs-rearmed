// The open-file limit (tools/ws7/recomp/runtime/rc_nofile.h): from launchd's soft limit of 256 it goes up far
// enough to hold more files than that, and a limit that is already higher stays as it is.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rc_nofile.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define FILES 1000

static uint64_t soft(void) {
  struct rlimit rl;
  CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0);
  return rl.rlim_cur;
}

static void set_soft(uint64_t n) {
  struct rlimit rl;
  CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0);
  rl.rlim_cur = n;
  CHECK(setrlimit(RLIMIT_NOFILE, &rl) == 0);
}

int main(void) {
  uint64_t before;
  int failed;

  set_soft(256);
  uint64_t now = rc_raise_nofile(&before, &failed);
  CHECK(before == 256 && !failed);
  CHECK(now > FILES && now == soft());
  printf("open-file limit %llu -> %llu\n", (unsigned long long)before, (unsigned long long)now);
  for (int k = 0; k < FILES; k++) CHECK(open("/dev/null", O_RDONLY) >= 0);

  set_soft(now + 1);  // above what it asks for: left alone
  CHECK(rc_raise_nofile(&before, &failed) == now + 1);
  CHECK(before == now + 1 && !failed);
  puts("ok");
  return 0;
}
