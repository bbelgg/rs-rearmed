// Renderer choice and the Metal crash fallback: see rsp_renderer.h.
#include "rsp_renderer.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

int rsp_renderer_parse(const char *s) {
  if (!s) return RSP_GFX_NONE;
  while (isspace((unsigned char)*s)) s++;
  size_t n = strcspn(s, " \t\r\n");
  const char *rest = s + n;
  while (isspace((unsigned char)*rest)) rest++;
  if (*rest) return RSP_GFX_NONE;
  if (n == 5 && !strncasecmp(s, "metal", 5)) return RSP_GFX_METAL;
  if ((n == 2 && !strncasecmp(s, "gl", 2)) || (n == 6 && !strncasecmp(s, "opengl", 6))) return RSP_GFX_GL;
  return RSP_GFX_NONE;
}

rsp_renderer_out rsp_renderer_decide(const rsp_renderer_in *in) {
  rsp_renderer_out o = {RSP_GFX_GL, RSP_RENDERER_NO_RSGL, 0};
  if (in->loaded == RSP_GFX_NONE) return o;
  int env = rsp_renderer_parse(in->env), set = rsp_renderer_parse(in->setting);
  if (env == RSP_GFX_GL) {
    o.reason = RSP_RENDERER_ENV;
  } else if (env == RSP_GFX_METAL) {
    // Forced: the crash count does not apply, but Metal still has to be able to start.
    if (in->metal_ok) o.backend = RSP_GFX_METAL;
    o.reason = in->metal_ok ? RSP_RENDERER_ENV : RSP_RENDERER_NO_METAL;
  } else if (set == RSP_GFX_GL) {
    o.reason = RSP_RENDERER_SETTING;
  } else if (in->abnormal >= RSP_RENDERER_CRASH_LIMIT) {
    o.reason = RSP_RENDERER_CRASHES;
  } else if (!in->metal_ok) {
    o.reason = RSP_RENDERER_NO_METAL;
  } else {
    o.backend = RSP_GFX_METAL;
    o.reason = set == RSP_GFX_METAL ? RSP_RENDERER_SETTING : RSP_RENDERER_DEFAULT;
  }
  // A restart sets RS_GL_BACKEND to the decision; when it says so already, libRSOpenGL would
  // load the same way again (no loop).
  o.reexec = o.backend != in->loaded && env != o.backend;
  return o;
}

const char *rsp_renderer_name(int backend) { return backend == RSP_GFX_METAL ? "metal" : "opengl"; }

const char *rsp_renderer_reason_text(int reason) {
  switch (reason) {
    case RSP_RENDERER_DEFAULT: return "default";
    case RSP_RENDERER_ENV: return "RS_GL_BACKEND";
    case RSP_RENDERER_SETTING: return "setting";
    case RSP_RENDERER_CRASHES: return "Metal ended abnormally twice in a row";
    case RSP_RENDERER_NO_METAL: return "Metal cannot start here";
    case RSP_RENDERER_NO_RSGL: return "no libRSOpenGL";
  }
  return "?";
}

// --- Files -------------------------------------------------------------------------------------

int rsp_renderer_home(char *out, size_t n, const char *bundle) {
  const char *e = getenv("RS_NATIVE_HOME");
  if (e && *e) return snprintf(out, n, "%s", e) < (int)n;
  if (bundle && *bundle) {
    // <home>/game/<app>: an install (install.sh writes <home>/game-dir).
    char b[1024], p[1100];
    snprintf(b, sizeof b, "%s", bundle);
    size_t l = strlen(b);
    while (l > 1 && b[l - 1] == '/') b[--l] = 0;
    char *s = strrchr(b, '/');
    if (s && s > b) {
      *s = 0;  // <home>/game
      char *g = strrchr(b, '/');
      if (g && !strcmp(g + 1, "game")) {
        *g = 0;  // <home>
        snprintf(p, sizeof p, "%s/game-dir", b);
        if (access(p, F_OK) == 0) return snprintf(out, n, "%s", b) < (int)n;
      }
    }
  }
  const char *h = getenv("HOME");
  if (!h || !*h) return 0;
  if (snprintf(out, n, "%s/Library/Caches/rs-rearmed", h) >= (int)n) return 0;
  char p[1100];
  snprintf(p, sizeof p, "%s/Library/Caches", h);
  mkdir(p, 0755);
  mkdir(out, 0755);
  return 1;
}

static int read_line(const char *path, char *buf, size_t n) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  if (!fgets(buf, (int)n, f)) buf[0] = 0;
  fclose(f);
  buf[strcspn(buf, "\r\n")] = 0;
  return 1;
}

// Writes `text` to path through a temporary file and a rename.
static int write_file(const char *path, const char *text) {
  char tmp[1100];
  snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
  FILE *f = fopen(tmp, "w");
  if (!f) return -1;
  int ok = fputs(text, f) >= 0;
  ok &= fclose(f) == 0;
  if (!ok || rename(tmp, path)) {
    unlink(tmp);
    return -1;
  }
  return 0;
}

int rsp_renderer_read_setting(const char *home, char *buf, size_t n) {
  char p[1100];
  snprintf(p, sizeof p, "%s/renderer", home);
  return read_line(p, buf, n);
}

int rsp_renderer_write_setting(const char *home, int backend) {
  char p[1100], t[16];
  snprintf(p, sizeof p, "%s/renderer", home);
  snprintf(t, sizeof t, "%s\n", rsp_renderer_name(backend));
  return write_file(p, t);
}

int rsp_metal_abnormal_count(const char *home) {
  char p[1100], b[64];
  snprintf(p, sizeof p, "%s/metal-session", home);
  if (!read_line(p, b, sizeof b)) return 0;
  int c = atoi(b);
  return (c > 0 ? c : 0) + 1;
}

static char g_marker[1100];
static volatile sig_atomic_t g_marker_set;

void rsp_metal_session_end(void) {
  if (g_marker_set) {
    g_marker_set = 0;
    unlink(g_marker);
  }
}

static void polite_stop(int sig) {
  rsp_metal_session_end();
  signal(sig, SIG_DFL);
  raise(sig);
}

int rsp_metal_session_begin(const char *home, int abnormal) {
  char t[64];
  snprintf(g_marker, sizeof g_marker, "%s/metal-session", home);
  snprintf(t, sizeof t, "%d\n", abnormal);
  if (write_file(g_marker, t)) return -1;
  g_marker_set = 1;
  static int hooked;
  if (!hooked) {
    hooked = 1;
    atexit(rsp_metal_session_end);  // exit(): the game's own orderly ends
    // A polite stop (logout, kill, Ctrl+C) is not Metal's fault; a handler someone else set stays.
    int sigs[] = {SIGTERM, SIGINT, SIGHUP};
    for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) {
      struct sigaction old;
      if (sigaction(sigs[i], NULL, &old) == 0 && old.sa_handler == SIG_DFL && !(old.sa_flags & SA_SIGINFO))
        signal(sigs[i], polite_stop);
    }
  }
  return 0;
}

void rsp_metal_session_forget(const char *home) {
  char p[1100];
  snprintf(p, sizeof p, "%s/metal-session", home);
  unlink(p);
}

// --- Startup -----------------------------------------------------------------------------------

static char **g_argv;

// `why`: what the restarted process reports (a reason number, or text for a late fallback).
static int exec_with(const char *backend_env, const char *why) {
  char exe[4096];
  uint32_t n = sizeof exe;
  if (!g_argv || _NSGetExecutablePath(exe, &n) != 0) return -1;
  setenv("RS_GL_BACKEND", backend_env, 1);
  setenv("RS_RENDERER_FALLBACK", why, 1);
  fflush(stdout);
  fflush(stderr);
  execv(exe, g_argv);
  fprintf(stderr, "[rsp] renderer: cannot restart %s: %s\n", exe, strerror(errno));
  return -1;
}

int rsp_renderer_restart_gl(const char *why) {
  rsp_metal_session_end();
  fprintf(stderr, "[rsp] %s; starting again with OpenGL\n", why);
  return exec_with("gl", why);
}

// A notification banner, fire and forget (osascript, double fork: never waited for).
static void notify(const char *text) {
  const char *e = getenv("RS_RENDERER_NOTIFY");
  if (e && *e == '0') return;
  char script[600];
  snprintf(script, sizeof script, "display notification \"%s\" with title \"RS Re-ARMed\"", text);
  pid_t p = fork();
  if (p == 0) {
    char *args[] = {"/usr/bin/osascript", "-e", script, NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t q;
    posix_spawn(&q, args[0], &fa, NULL, args, environ);
    _exit(0);
  }
  if (p > 0) waitpid(p, NULL, 0);
}

rsp_renderer_out rsp_renderer_startup(char **argv, const char *bundle, int loaded, int (*metal_ok)(void),
                                      int flags) {
  g_argv = argv;
  char home[1024], setting[64];
  int have_home = rsp_renderer_home(home, sizeof home, bundle);
  rsp_renderer_in in = {getenv("RS_GL_BACKEND"), NULL, 0, 1, loaded};
  if (have_home) {
    if (rsp_renderer_read_setting(home, setting, sizeof setting)) in.setting = setting;
    in.abnormal = rsp_metal_abnormal_count(home);
  }
  if (in.setting && *in.setting && rsp_renderer_parse(in.setting) == RSP_GFX_NONE)
    fprintf(stderr, "[rsp] renderer: %s/renderer says \"%s\" (metal or opengl); ignored\n", home, in.setting);
  rsp_renderer_out o = rsp_renderer_decide(&in);
  if (o.backend == RSP_GFX_METAL && metal_ok && !metal_ok()) {
    in.metal_ok = 0;
    o = rsp_renderer_decide(&in);
  }
  const char *restarted = getenv("RS_RENDERER_FALLBACK");
  if (o.reason == RSP_RENDERER_CRASHES) {
    fprintf(stderr,
            "[rsp] Metal ended abnormally twice in a row; starting with OpenGL. Set the renderer to metal "
            "to try again (echo metal > \"%s/renderer\")\n",
            home);
    // From now on OpenGL by the setting, and the count starts again for the next Metal try.
    rsp_renderer_write_setting(home, RSP_GFX_GL);
    rsp_metal_session_forget(home);
    char p[1100], note[1600], ts[32];
    time_t t = time(NULL);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
    snprintf(p, sizeof p, "%s/renderer-fallback.txt", home);
    snprintf(note, sizeof note,
             "%s: the last two starts with Metal ended abnormally (a crash, or a force quit), so RS Re-ARMed "
             "switched its renderer to OpenGL: the file \"renderer\" in this folder now says opengl.\n"
             "To try Metal again: echo metal > \"%s/renderer\"\n",
             ts, home);
    FILE *f = fopen(p, "a");
    if (f) {
      fputs(note, f);
      fclose(f);
    }
    if (flags & RSP_RENDERER_SESSION)
      notify("Metal ended abnormally twice in a row, so the game now starts with OpenGL. See "
             "renderer-fallback.txt in the RS Re-ARMed folder to switch back.");
  }
  if (o.reexec) {
    char why[16];
    snprintf(why, sizeof why, "%d", o.reason);
    if (o.reason == RSP_RENDERER_NO_METAL) fprintf(stderr, "[rsp] no usable Metal device; starting with OpenGL\n");
    exec_with(o.backend == RSP_GFX_METAL ? "metal" : "gl", why);
    o.backend = loaded;  // the exec failed: go on with what is loaded
    o.reexec = 0;
  }
  // Restarted by the above: RS_GL_BACKEND is ours, report why it was set. A late fallback's text
  // (rsp_renderer_restart_gl) is logged as is.
  const char *late = NULL;
  if (restarted && o.reason == RSP_RENDERER_ENV) {
    char *end;
    long r = strtol(restarted, &end, 10);
    if (*restarted && !*end && r > RSP_RENDERER_ENV && r <= RSP_RENDERER_NO_RSGL) o.reason = (int)r;
    else late = restarted;
  }
  fprintf(stderr, "[rsp] renderer: %s (%s%s%s%s)\n", rsp_renderer_name(o.backend), rsp_renderer_reason_text(o.reason),
          restarted ? "; restarted with RS_GL_BACKEND set" : "", late ? " after: " : "", late ? late : "");
  unsetenv("RS_RENDERER_FALLBACK");
  if (o.backend == RSP_GFX_METAL && (flags & RSP_RENDERER_SESSION) && have_home)
    rsp_metal_session_begin(home, in.abnormal);
  return o;
}
