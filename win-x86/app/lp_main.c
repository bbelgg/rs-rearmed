// Learn & Play main: map the game and the DLLs translated with it into the 4 GB window, bind
// their imports, start the DLLs, and run the game's entry point.
//
// The images (gen_image_pe.py trees linked into this binary):
//   msvcr100        also answers the msvcrt.dll imports of d3dx9_42 and D3DCompiler_42
//   msvcp100
//   d3dx9_42
//   d3dcompiler_42  rebased to 0x11000000; deferred: d3dx loads it with LoadLibraryA
//   bink2w32        rebased to 0x12000000; the game imports it (else the stubs in win32_misc.c)
//   the game        no --module: the plain rc32_mod
// Everything else the images import is the host's (win-x86/shims: w32_import by name).
//
// env (defaults beside the executable; in an app bundle, see "installed app" below):
//   LP_IMAGE     the game image (lp_image.py)            default <exe dir>/lp_game.exe
//   LP_DLL_DIR   the DLL images, msvcr100.dll, ...        default <exe dir>/dlls
//   LP_GAME_DIR  the Windows game folder ("C:\Rocksmith2014" to the game; read-only: its writes
//                go to LP_USER_DIR/game)                  default <exe dir>/game
//   LP_USER_DIR  every other Windows path                 default <exe dir>/userdata
//   LP_HOME      the installed app's folder (below)
//   LP_ARGS      appended to the game's command line
//   RS_REARMED   0: the game's own title logo (see title_logo)
//   LP_LOG       1: first call of each import, 2: every import call
//   LP_LOG_FILES 1: every file the game opens
//   LP_LOG_TO    append the log (stderr) to this file (an app started with `open` has no terminal)
//   LP_FRAME_LOG frames longer than this many ms are logged with what happened in them
//   LP_QOS       0: the default QoS for the game's threads (not user-interactive / -initiated)
//   RS_LOAD_MODS 0: no mods
//   RS_MODS_PATH the mods: a folder or one library     default LP_HOME/mods (an installed app)
//   RC_HOOK_TRACE hook points to log the raw calls of (rc_hook.h)
//
// The installed app (win-x86/install/lp_install.py app-install): the executable in
// <app>/Contents/MacOS, the image and the DLLs in <app>/Contents/Resources, and everything that
// belongs to the user in LP_HOME (default ~/Library/Application Support/RS Re-ARMed/learn-play),
// as the Mac launcher keeps it in its own folder:
//   game-dir     the Windows game folder, one line (LP_GAME_DIR overrides it): the app's own copy of
//                the game files, game/ (lp_install.py game-copy), so a Steam update or a cleared
//                download folder does not change them. Without game-dir: game/. The copy is complete
//                when game/.rearmed/manifest.json is there; else the app says to refresh it and quits.
//                A game on another drive: game/ is a link to the copy on that drive; when that drive is
//                not connected the app says so and quits.
//   game/.rearmed/steam.env  RS_STEAM_* defaults (the installed DLC and the AppIDs the dlc/
//                archives carry, the language, the account: the one whose license has Learn & Play)
//                for the offline Steam layer, recorded from Steam when the copy was made
//   settings.env the built-in mods switched off in the installer (lp_settings.h), as the Mac launcher's
//   userdata/    LP_USER_DIR: Rocksmith.ini and the game's other writes
//   steam/       RS_STEAM_DATA_DIR: the offline Steam layer's saves (profiles, progress)
//   logs/        last-run.log and previous-run.log (stderr; LP_LOG_TO overrides it)
//   mods/        every .dylib here is loaded at the start, as on the Mac (mods_guard, rc_mods.c)
// Nothing there is in the bundle, so a reinstall keeps it, and the bundle's signature stays whole.
// The Metal disk cache is the app's own too (RS_METAL_CACHE_DIR, default
// ~/Library/Caches/rs-rearmed/learn-play/metal), beside the Mac edition's .../rs-rearmed/metal.
#include <CoreFoundation/CoreFoundation.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../online/online32.h"
#include "lp_settings.h"
#include "rc32.h"
#include "rc_hook.h"
#include "win32_shims.h"

// The 64-bit runtime pieces linked here (rc_tramp, rc_setjmp, rc_thread) look functions up by
// address; for this process that is the 32-bit table.
rc_fn rc_lookup(uint64_t addr) { return rc32_lookup((uint32_t)addr); }

extern rc32_module msvcr100_rc32_mod, msvcp100_rc32_mod, d3dx9_42_rc32_mod;
// optional: without it LoadLibrary("d3dcompiler_42.dll") fails and D3DX cannot compile HLSL
extern rc32_module d3dcompiler_42_rc32_mod __attribute__((weak));
// optional: without it the game's Bink imports go to the seam's stubs (no videos)
extern rc32_module bink2w32_rc32_mod __attribute__((weak));

static char exe_dir[PATH_MAX];
static const char *dflt_env(const char *var, const char *rel) {
  const char *v = getenv(var);
  if (!v || !*v) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", exe_dir, rel);
    setenv(var, p, 1);
    v = getenv(var);
  }
  return v;
}

// var, if unset, to dir/rel
static void dflt_env_in(const char *var, const char *dir, const char *rel) {
  const char *v = getenv(var);
  if (v && *v) return;
  char p[PATH_MAX];
  snprintf(p, sizeof p, "%s/%s", dir, rel);
  setenv(var, p, 1);
}

static void mkdir_p(const char *path) {
  char p[PATH_MAX];
  snprintf(p, sizeof p, "%s", path);
  for (char *s = p + 1; *s; s++)
    if (*s == '/') {
      *s = 0;
      mkdir(p, 0755);
      *s = '/';
    }
  mkdir(p, 0755);
}

// A message box before anything else runs (the app has no window yet), then quit.
static void fail_alert(const char *msg) {
  fprintf(stderr, "lp: %s\n", msg);
  CFStringRef t = CFStringCreateWithCString(0, "RS Re-ARMed Learn & Play", kCFStringEncodingUTF8);
  CFStringRef m = CFStringCreateWithCString(0, msg, kCFStringEncodingUTF8);
  CFOptionFlags r;
  if (t && m) CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, 0, 0, 0, t, m, 0, 0, 0, &r);
  if (t) CFRelease(t);
  if (m) CFRelease(m);
  exit(1);
}

// A game on another drive: the copy is on that drive too and game/ is a link to it (gamefiles.py). 1 with
// MSG when the link leads nowhere: that drive not connected, or the copy on it gone.
static int game_copy_missing(const char *game, char *msg, size_t n) {
  struct stat st, vs;
  char t[PATH_MAX], target[PATH_MAX], vol[PATH_MAX] = "", vp[PATH_MAX];
  if (lstat(game, &st) != 0 || !S_ISLNK(st.st_mode) || stat(game, &st) == 0) return 0;
  ssize_t len = readlink(game, t, sizeof t - 1);
  if (len <= 0) return 0;
  t[len] = 0;
  if (t[0] == '/') {
    snprintf(target, sizeof target, "%s", t);
  } else {
    const char *slash = strrchr(game, '/');
    snprintf(target, sizeof target, "%.*s%s", slash ? (int)(slash - game + 1) : 0, game, t);
  }
  if (!strncmp(target, "/Volumes/", 9) && target[9] && target[9] != '/')
    snprintf(vol, sizeof vol, "%.*s", (int)strcspn(target + 9, "/"), target + 9);
  // mounted: a folder in /Volumes on a volume of its own (not one left there)
  snprintf(vp, sizeof vp, "/Volumes/%s", vol);
  if (*vol && !(stat(vp, &st) == 0 && S_ISDIR(st.st_mode) && stat("/Volumes", &vs) == 0 && st.st_dev != vs.st_dev))
    snprintf(msg, n, "The drive with the game files (%s) isn't connected. Connect it and start RS Re-ARMed "
             "Learn & Play again.", vol);
  else
    snprintf(msg, n, "The game files on the other drive are gone (%s is not there). Run the RS Re-ARMed "
             "Installer again for a new copy.", target);
  return 1;
}

// The title logo, as the Mac launcher does it: in the copy of the game files static.psarc is a link to
// static.psarc.original or, when the build made it and unless RS_REARMED=0, to static.psarc.rearmed.
// A folder without static.psarc.original (the depot's own, LP_GAME_DIR) is left as it is.
static void title_logo(const char *game) {
  char orig[PATH_MAX], logo[PATH_MAX], link[PATH_MAX], tmp[PATH_MAX], now[PATH_MAX];
  snprintf(orig, sizeof orig, "%s/static.psarc.original", game);
  snprintf(logo, sizeof logo, "%s/static.psarc.rearmed", game);
  snprintf(link, sizeof link, "%s/static.psarc", game);
  snprintf(tmp, sizeof tmp, "%s/static.psarc.link", game);
  if (access(orig, F_OK) != 0) return;
  const char *off = getenv("RS_REARMED");
  const char *want = access(logo, F_OK) == 0 && !(off && !strcmp(off, "0")) ? "static.psarc.rearmed"
                                                                            : "static.psarc.original";
  ssize_t n = readlink(link, now, sizeof now - 1);
  if (n > 0) {
    now[n] = 0;
    if (!strcmp(now, want)) return;
  }
  unlink(tmp);
  if (symlink(want, tmp) != 0 || rename(tmp, link) != 0)
    fprintf(stderr, "lp: static.psarc not linked to %s: %s\n", want, strerror(errno));
}

// game/.rearmed/steam.env: KEY=VALUE lines, RS_STEAM_* only, each unless set already. Lines of any
// length: RS_STEAM_DLC lists every owned DLC and every AppID the song archives carry.
static void steam_env_defaults(const char *game) {
  char p[PATH_MAX], *line = NULL;
  size_t cap = 0;
  snprintf(p, sizeof p, "%s/.rearmed/steam.env", game);
  FILE *f = fopen(p, "r");
  if (!f) return;
  while (getline(&line, &cap, f) > 0) {
    line[strcspn(line, "\r\n")] = 0;
    char *eq = strchr(line, '=');
    if (!eq || strncmp(line, "RS_STEAM_", 9) || !eq[1]) continue;
    *eq = 0;
    setenv(line, eq + 1, 0);
  }
  free(line);
  fclose(f);
}

// Without Steam's list of accounts (Steam uninstalled), the account whose saves are in the Steam
// layer's folder, when there is exactly one: its saves stay this app's saves.
static void steam_account_default(const char *home_dir) {
  const char *id = getenv("RS_STEAM_ID");
  if ((id && *id) || !home_dir) return;
  char p[PATH_MAX];
  snprintf(p, sizeof p, "%s/Library/Application Support/Steam/config/loginusers.vdf", home_dir);
  if (access(p, F_OK) == 0) return;
  const char *data = getenv("RS_STEAM_DATA_DIR");
  DIR *d = data ? opendir(data) : 0;
  if (!d) return;
  unsigned long long account = 0;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    char *end;
    unsigned long long a = strtoull(e->d_name, &end, 10);
    snprintf(p, sizeof p, "%s/%s/221680", data, e->d_name);
    struct stat st;
    if (*e->d_name && !*end && a && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
      account = a;
      n++;
    }
  }
  closedir(d);
  if (n == 1) {
    snprintf(p, sizeof p, "%llu", 76561197960265728ULL + account);
    setenv("RS_STEAM_ID", p, 1);
  }
}

// The installed app's Metal disk cache, the game's and the warm-up's.
static void metal_cache_default(void) {
  char caches[PATH_MAX];
  snprintf(caches, sizeof caches, "%s/Library/Caches/rs-rearmed/learn-play", getenv("HOME") ? getenv("HOME") : "/tmp");
  dflt_env_in("RS_METAL_CACHE_DIR", caches, "metal");
}

static int in_bundle(void) {
  size_t n = strlen(exe_dir);
  static const char tail[] = ".app/Contents/MacOS";
  return n >= sizeof tail && !strcmp(exe_dir + n - (sizeof tail - 1), tail);
}

static void user_home(char *home, size_t n) {
  const char *h = getenv("LP_HOME");
  if (h && *h)
    snprintf(home, n, "%s", h);
  else
    snprintf(home, n, "%s/Library/Application Support/RS Re-ARMed/learn-play", getenv("HOME") ? getenv("HOME") : "/tmp");
}

// What to load from the mods folder, and the network guard before it. The game itself has no way to the
// network here (win32_net.c refuses its calls, and no host socket call is linked in), but a mod is native
// code and could make one; so with mods the process runs as the Mac edition's always does, with libnetguard
// inserted, which takes starting again: dyld only inserts at a start. No guard, no mods. The message waits
// for the log (mods_message).
static const char *mods_folder, *mods_message;

static int netguard_loaded(void) {
  char path[PATH_MAX], want[PATH_MAX], got[PATH_MAX];
  snprintf(path, sizeof path, "%s/libnetguard.dylib", exe_dir);
  if (!realpath(path, want)) return 0;
  for (uint32_t k = 0, n = _dyld_image_count(); k < n; k++) {
    const char *name = _dyld_get_image_name(k);
    if (strstr(name, "/libnetguard.dylib") && realpath(name, got) && !strcmp(got, want)) return 1;
  }
  return 0;
}

static void mods_guard(char **argv) {
  static char dflt[PATH_MAX], guard[PATH_MAX], log[PATH_MAX];
  if (in_bundle()) {
    char home[PATH_MAX];
    user_home(home, sizeof home);
    snprintf(dflt, sizeof dflt, "%s/mods", home);
    snprintf(log, sizeof log, "%s/logs", home);
  }
  const char *where = rc_mods_where(dflt);
  if (!where) return;
  if (netguard_loaded()) {
    mods_folder = where;
    return;
  }
  if (!rc_mods_count(where)) return;  // a mod copied in from here on waits for the next start
  snprintf(guard, sizeof guard, "%s/libnetguard.dylib", exe_dir);
  if (access(guard, R_OK) != 0) {
    mods_message = "[rc] mods not loaded: no libnetguard.dylib beside the app";
    return;
  }
  if (getenv("LP_MODS_GUARD")) {
    mods_message = "[rc] mods not loaded: the network guard did not load";
    return;
  }
  setenv("LP_MODS_GUARD", "1", 1);
  const char *inserted = getenv("DYLD_INSERT_LIBRARIES");
  char *both = NULL;
  if (inserted && *inserted && asprintf(&both, "%s:%s", guard, inserted) < 0) both = NULL;
  setenv("DYLD_INSERT_LIBRARIES", both ? both : guard, 1);
  if (*log) {
    mkdir_p(log);
    strlcat(log, "/netguard.log", sizeof log);
    setenv("RS_NETGUARD_LOG", log, 0);
  }
  char self[PATH_MAX];
  uint32_t sz = sizeof self;
  if (!_NSGetExecutablePath(self, &sz)) execv(self, argv);
  mods_message = "[rc] mods not loaded: the app could not start again with the network guard";
}

// An installed app: the defaults from LP_HOME (see the top of this file). 0 when this executable
// is not in an app bundle (a build's out/ directory: the defaults beside it).
static int installed_defaults(void) {
  if (!in_bundle()) return 0;
  size_t n = strlen(exe_dir);
  char res[PATH_MAX], home[PATH_MAX];
  snprintf(res, sizeof res, "%.*s/Resources", (int)(n - strlen("/MacOS")), exe_dir);
  dflt_env_in("LP_IMAGE", res, "lp_game.exe");
  dflt_env_in("LP_DLL_DIR", res, "dlls");
  user_home(home, sizeof home);
  setenv("LP_HOME", home, 1);
  if (!getenv("LP_GAME_DIR") || !*getenv("LP_GAME_DIR")) {
    char f[PATH_MAX], line[PATH_MAX] = "";
    snprintf(f, sizeof f, "%s/game-dir", home);
    FILE *g = fopen(f, "r");
    if (g) {
      if (!fgets(line, sizeof line, g)) line[0] = 0;
      fclose(g);
    }
    line[strcspn(line, "\r\n")] = 0;
    if (*line)
      setenv("LP_GAME_DIR", line, 1);
    else
      dflt_env_in("LP_GAME_DIR", home, "game");
    // the installed game files: the app's own copy, complete only with its manifest (written last)
    char m[PATH_MAX], meta[PATH_MAX], why[2 * PATH_MAX];
    if (game_copy_missing(getenv("LP_GAME_DIR"), why, sizeof why)) fail_alert(why);
    snprintf(meta, sizeof meta, "%s/.rearmed", getenv("LP_GAME_DIR"));
    snprintf(m, sizeof m, "%s/manifest.json", meta);
    // macOS's privacy answer for a folder or a drive reads as "not permitted", not as a missing file
    if (access(m, F_OK) != 0 && (errno == EPERM || errno == EACCES))
      fail_alert("macOS is not letting RS Re-ARMed Learn & Play read the game files. In System Settings, open "
                 "Privacy & Security, then Files & Folders, turn on Removable Volumes (or the folder) for RS "
                 "Re-ARMed Learn & Play, and start it again.");
    if (access(m, F_OK) != 0)
      fail_alert(access(meta, F_OK) == 0
                     ? "The copy of the game files is incomplete (a copy or refresh was stopped). Open the RS "
                       "Re-ARMed Installer and click Refresh Game Files (or run tools/play-native/learn-play.sh "
                       "--refresh)."
                     : "RS Re-ARMed Learn & Play has no copy of the game files. Run the RS Re-ARMed Installer "
                       "(or tools/play-native/learn-play.sh) again.");
  }
  dflt_env_in("LP_USER_DIR", home, "userdata");
  dflt_env_in("RS_STEAM_DATA_DIR", home, "steam");
  metal_cache_default();
  mkdir_p(getenv("LP_USER_DIR"));
  mkdir_p(getenv("RS_STEAM_DATA_DIR"));
  steam_env_defaults(getenv("LP_GAME_DIR"));
  steam_account_default(getenv("HOME"));
  // stderr to logs/last-run.log (the previous run's kept), unless it goes elsewhere already: an
  // app started from the Finder has no terminal
  const char *log_to = getenv("LP_LOG_TO");
  if ((!log_to || !*log_to) && !isatty(2)) {
    char logs[PATH_MAX], last[PATH_MAX], prev[PATH_MAX];
    snprintf(logs, sizeof logs, "%s/logs", home);
    mkdir_p(logs);
    snprintf(last, sizeof last, "%s/last-run.log", logs);
    snprintf(prev, sizeof prev, "%s/previous-run.log", logs);
    if (rename(last, prev) && errno != ENOENT) unlink(last);
    if (freopen(last, "w", stderr)) setvbuf(stderr, 0, _IOLBF, 0);
  }
  return 1;
}

// a fault in translated code: say where in the guest window it was
static void on_fault(int sig, siginfo_t *si, void *uc) {
  (void)uc;
  uint8_t *a = si->si_addr;
  char m[160];
  int n;
  if (rc_mem && a >= rc_mem && a < rc_mem + (1ull << 32))
    n = snprintf(m, sizeof m, "\nlp: signal %d: guest address 0x%x\n", sig, (uint32_t)(a - rc_mem));
  else
    n = snprintf(m, sizeof m, "\nlp: signal %d: host address %p\n", sig, (void *)a);
  write(2, m, (size_t)n);
  signal(sig, SIG_DFL);
}

typedef struct { uint32_t entry; int rc; } run_arg;
static void *run_game(void *p) {
  run_arg *r = p;
  pthread_setname_np("w32 game");  // for sample, Instruments and LP_THREAD_CPU
  rc_ctx *c = rc32_thread_ctx();
  fprintf(stderr, "lp: starting the DLLs (DllMain PROCESS_ATTACH)\n");
  if (rc32_images_init(c)) {
    fprintf(stderr, "lp: a DLL failed to start\n");
    exit(1);
  }
  fprintf(stderr, "lp: DLLs started; entering the game at 0x%x (WinMainCRTStartup)\n", r->entry);
  uint32_t code = rc32_call(c, r->entry, 0, 0, 0, 0, 0);
  fprintf(stderr, "lp: the game's entry returned %u\n", code);
  rc32_images_fini();
  r->rc = (int)code;
  w32_host_exit((int)code);  // the game's other threads still run, as at ExitProcess
}

void lp_window_loop(void);  // lp_window.m
void lp_mods_init(void);    // win32_overrides.c

int rs_gfx_warm_only(double max_seconds, double interval, FILE *out);  // libRSOpenGL

// --warm-shaders: the install's shader warm-up (tools/play-native/warm-shaders.sh), as the Mac app's:
// no window, nothing of the game loaded, in the installed app's own executable because Metal's
// shader cache belongs to the app. Exit 0 ready, 1 timed out, 2 no Metal.
static int warm_shaders(void) {
  if (in_bundle()) metal_cache_default();
  const char *e = getenv("RS_METAL_WARM_MAX_SECONDS");
  double max_s = e && *e ? atof(e) : 3600;
  setvbuf(stdout, 0, _IOLBF, 0);
  return rs_gfx_warm_only(max_s > 0 ? max_s : 3600, 0.5, stdout);
}

int main(int argc, char **argv) {
  char self[PATH_MAX];
  uint32_t sz = sizeof self;
  if (_NSGetExecutablePath(self, &sz) || !realpath(self, exe_dir)) snprintf(exe_dir, sizeof exe_dir, ".");
  char *sl = strrchr(exe_dir, '/');
  if (sl) *sl = 0;
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--warm-shaders")) return warm_shaders();
  // An installed app's settings.env, before anything reads a switch (RS_LOAD_MODS in mods_guard, RS_REARMED, the
  // mods' own); logged once the log is set up.
  static char settings[PATH_MAX];
  if (in_bundle()) {
    char home[PATH_MAX];
    user_home(home, sizeof home);
    snprintf(settings, sizeof settings, "%s/settings.env", home);
    lp_settings_apply(settings);
  }
  mods_guard(argv);  // may start this process again: before the log is turned over
  int installed = installed_defaults();
  const char *image = dflt_env("LP_IMAGE", "lp_game.exe");
  const char *dlls = dflt_env("LP_DLL_DIR", "dlls");
  dflt_env("LP_GAME_DIR", "game");
  dflt_env("LP_USER_DIR", "userdata");
  const char *log_to = getenv("LP_LOG_TO");
  if (log_to && *log_to && freopen(log_to, "a", stderr)) setvbuf(stderr, 0, _IOLBF, 0);
  fprintf(stderr, "lp: game folder %s, user folder %s\n", getenv("LP_GAME_DIR"), getenv("LP_USER_DIR"));
  if (installed) fprintf(stderr, "lp: installed app, settings and saves in %s\n", getenv("LP_HOME"));
  if (*settings) lp_settings_log(settings, stderr);
  title_logo(getenv("LP_GAME_DIR"));
  lp_mods_init();

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_fault;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaction(SIGSEGV, &sa, 0);
  sigaction(SIGBUS, &sa, 0);

  if (rc32_mem_init()) return 1;
  w32_init();

  static const struct { rc32_module *m; const char *file; int defer; } dll[] = {
      {&msvcr100_rc32_mod, "msvcr100.dll", 0},
      {&msvcp100_rc32_mod, "msvcp100.dll", 0},
      {&d3dx9_42_rc32_mod, "d3dx9_42.dll", 0},
      {&d3dcompiler_42_rc32_mod, "d3dcompiler_42.dll", 1},
      {&bink2w32_rc32_mod, "bink2w32.dll", 0},
  };
  for (size_t k = 0; k < sizeof dll / sizeof dll[0]; k++) {
    if (!dll[k].m) {
      fprintf(stderr, "lp: %s not built in: LoadLibrary of it fails\n", dll[k].file);
      continue;
    }
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", dlls, dll[k].file);
    rc32_image *im = rc32_image_add(dll[k].m, p);
    if (!im) {
      fprintf(stderr, "lp: cannot map %s\n", p);
      return 1;
    }
    if (dll[k].defer) rc32_image_defer(im);
    fprintf(stderr, "lp: mapped %s at 0x%x (%d functions)\n", dll[k].file, rc32_image_base(dll[k].file), *dll[k].m->table_len);
  }
  rc32_image_alias("msvcr100.dll", "msvcrt.dll");  // d3dx9_42's and D3DCompiler_42's C runtime
  if (!rc32_image_add(&rc32_mod, image)) {
    fprintf(stderr, "lp: cannot map the game image %s\n", image);
    return 1;
  }
  // native libm for msvcr100's _CIpow & co. (docs/windows/DESIGN.md "Libraries"), before the binding
  fprintf(stderr, "lp: %d native msvcr100 math overrides\n", w32_crtmath_register());
  fprintf(stderr, "lp: %d native msvcr100 memory overrides\n", w32_crtmem_register());
  if (rc32_images_bind()) {
    fprintf(stderr, "lp: cannot bind the imports\n");
    return 1;
  }
  // The offline online layer (win-x86/online): RSConnect's vtables patched before the game runs.
  on32_install(&on32_learn_play);
  // The mods, with the images mapped and the game not started, as on the Mac: a mod's constructor registers
  // its hooks (rc_hook.h).
  if (mods_message) fprintf(stderr, "%s\n", mods_message);
  // A mod finds its settings in the game folder, the working directory on the Mac
  if (mods_folder && installed && chdir(getenv("LP_GAME_DIR")))
    fprintf(stderr, "lp: cannot go to the game folder %s: %s\n", getenv("LP_GAME_DIR"), strerror(errno));
  if (mods_folder) rc_mods_load(mods_folder);
  rc_hook_trace_init();
  uint32_t base = rc32_image_base(0);
  uint32_t lfanew = *(uint32_t *)RC32_H(base + 0x3c), entry = base + *(uint32_t *)RC32_H(base + lfanew + 40);
  fprintf(stderr, "lp: mapped the game at 0x%x (%d functions, %d imports), entry 0x%x\n", base, rc32_table_len,
          rc32_imports_len, entry);

  // the guest runs on a thread with a large host stack: translated code nests deeper there than
  // the x86 code did on its own stack
  run_arg r = {entry, 0};
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 256u << 20);
  // the game's thread runs the frame: the scheduler should not leave it waiting behind other
  // work on a busy machine (frames of 100 ms and more with it at the default QoS)
  // (LP_QOS=0: the default QoS everywhere, as before)
  const char *qos = getenv("LP_QOS");
  if (!qos || *qos != '0') pthread_attr_set_qos_class_np(&at, QOS_CLASS_USER_INTERACTIVE, 0);
  pthread_t t;
  if (pthread_create(&t, &at, run_game, &r)) {
    fprintf(stderr, "lp: cannot start the game thread\n");
    return 1;
  }
  pthread_detach(t);
  lp_window_loop();  // AppKit on the main thread; the game's thread exits the process
  return 0;
}
