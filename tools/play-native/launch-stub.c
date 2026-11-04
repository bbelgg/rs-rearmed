// The executable of RS Re-ARMed.app (install.sh compiles it): runs Contents/Resources/launch.sh and waits for it.
//
// launch.sh could be the bundle's executable itself, but then macOS sees /bin/bash, not this app: a privacy question
// about a game copy on another disk has no app to ask for and nothing to remember the answer by, and System Settings
// has no entry to turn on. With this program the app is the process macOS holds responsible for what the script
// does, so the question names RS Re-ARMed and the answer stays. It has to stay running: an exec would make it bash again.
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

extern char **environ;

int main(void) {
  char exe[PATH_MAX], real[PATH_MAX], script[PATH_MAX + 32];
  uint32_t n = sizeof exe;
  if (_NSGetExecutablePath(exe, &n) || !realpath(exe, real)) return 1;
  snprintf(script, sizeof script, "%s/../Resources/launch.sh", dirname(real));
  char *argv[] = {"/bin/bash", script, NULL};
  pid_t pid;
  if (posix_spawn(&pid, "/bin/bash", NULL, NULL, argv, environ)) return 1;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return 1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
