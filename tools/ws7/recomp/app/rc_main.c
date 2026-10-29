// main() of the arm64 Rocksmith 2014 app.
//
//   locate image -> dlopen the arm64 shim dylibs (RTLD_GLOBAL) -> rc_image_load
//   -> run the 3,487 __mod_init_func initializers translated -> fix up the ObjC
//   classrefs the shell replaces -> rsp_set_hooks -> rsp_shell_main (NSApp run).
//
// Flags (anywhere before the game's own args):
//   --init-only   load + initializers, print counts and timing, exit 0
//   --bind-only   load only (prints the bind statistics), exit 0
//   --warm-shaders  compile the Metal shaders (manifest + bundled corpus) into this app's caches and
//                 exit, before anything of the game is loaded: no window, no engine (the installer's
//                 warm-up, tools/play-native/warm-shaders.sh; rsp_mtl_warm_only). Runs with the
//                 Metal backend whatever RS_GL_BACKEND says (re-executes itself with it set);
//                 RS_METAL_WARM_MAX_SECONDS limits it (default 3600). Exit 0 ready, 1 timed out, 2 no Metal.
//   --renderer-check  print the renderer a game start would use now ("renderer: metal (default)")
//                 and exit 0, without a window or the game. Decides as a start does (rsp_renderer.h),
//                 restart and crash fallback included (that one switches the setting to opengl),
//                 but starts no Metal session (no crash marker).
// Renderer: before anything else main() decides Metal or OpenGL (rsp_renderer_startup: RS_GL_BACKEND,
// no Metal device, the Metal crash count, <home>/renderer, default Metal) and, when libRSOpenGL
// loaded with the other backend, executes itself again with RS_GL_BACKEND set.
// Environment:
//   RS_RC_IMAGE        the plain x86_64 image (default <bundle>/Contents/Resources/Rocksmith2014.x86_64)
//   RS_RC_SHIMS        extra dylibs to dlopen RTLD_GLOBAL before the image binds (colon list)
//   RS_RC_NO_SHIMS=1   do not load the bundled shims
//   RC_LOG_UNRESOLVED  rc_image logs every unresolved bind and its statistics
//   RC_TRACE_INITS=1   print each initializer before running it
//   RS_STEAM_MODE      defaults to "stub" (the game must not call home)
#include <CoreFoundation/CoreFoundation.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <execinfo.h>
#include <sys/ucontext.h>

#include "rc_rt.h"
#include "rsp_platform.h"
#include "rsp_renderer.h"
#include "seam.h"

int rsp_shell_main(int argc, const char **argv);  // shims/platform/src/rsp_shell.m
int rsp_mtl_warm_only(double max_seconds, double interval, FILE *out);  // rsp_present_metal.m
int rsp_mtl_loaded_backend(void);                                         // rsp_present_metal.m
int rsp_mtl_usable(void);                                                 // rsp_present_metal.m

static double now_s(void) {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

// ---------------------------------------------------------------------------
// Crash report: which phase / initializer was running (the translated code has
// no useful native symbols).
static volatile const char *g_phase = "start";
static volatile int g_init_idx = -1;
static volatile uint64_t g_init_fn;
static void u64hex(char *o, uint64_t v) {
  static const char d[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++) o[i] = d[(v >> (60 - 4 * i)) & 15];
  o[16] = 0;
}
static void crash_handler(int sig, siginfo_t *si, void *uc) {
  char b[17], m[160] = "[rc] fatal signal ";
  size_t n = strlen(m);
  m[n++] = '0' + sig / 10;
  m[n++] = '0' + sig % 10;
  m[n] = 0;
  write(2, m, n);
  write(2, " addr 0x", 8);
  u64hex(b, (uint64_t)si->si_addr);
  write(2, b, 16);
  write(2, " phase ", 7);
  write(2, (const void *)g_phase, strlen((const char *)g_phase));
  if (g_init_idx >= 0) {
    write(2, " init#", 6);
    char d[12];
    int k = 11, v = g_init_idx;
    d[k] = 0;
    do { d[--k] = '0' + v % 10; v /= 10; } while (v);
    write(2, d + k, 11 - k);
    write(2, " fn 0x", 6);
    u64hex(b, g_init_fn - rc_slide);
    write(2, b, 16);
  }
  if (rc_in_image((uint64_t)si->si_addr)) {
    write(2, " = image 0x", 11);
    u64hex(b, (uint64_t)si->si_addr - rc_slide);
    write(2, b, 16);
  }
  write(2, "\n", 1);
#ifdef __aarch64__
  ucontext_t *u = uc;
  write(2, "[rc] pc 0x", 10);
  u64hex(b, u->uc_mcontext->__ss.__pc);
  write(2, b, 16);
  write(2, " lr 0x", 6);
  u64hex(b, u->uc_mcontext->__ss.__lr);
  write(2, b, 16);
  write(2, "\n", 1);
#endif
  void *bt[64];
  int nb = backtrace(bt, 64);
  backtrace_symbols_fd(bt, nb, 2);
  signal(sig, SIG_DFL);  // the system crash report still follows
}
static void install_crash_handler(void) {
  static char alt[64 * 1024];
  stack_t ss = {alt, 0, sizeof alt};
  ss.ss_sp = alt;
  sigaltstack(&ss, 0);
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = crash_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  sigaction(SIGSEGV, &sa, 0);
  sigaction(SIGBUS, &sa, 0);
  sigaction(SIGILL, &sa, 0);
  sigaction(SIGABRT, &sa, 0);
}

// ---------------------------------------------------------------------------
// Locating things

static int bundle_path(char *out, size_t n) {
  CFBundleRef b = CFBundleGetMainBundle();
  if (!b) return 0;
  CFURLRef u = CFBundleCopyBundleURL(b);
  if (!u) return 0;
  int ok = CFURLGetFileSystemRepresentation(u, true, (UInt8 *)out, (CFIndex)n);
  CFRelease(u);
  return ok && strstr(out, ".app") != 0;
}

static int exists(const char *p) { return access(p, R_OK) == 0; }

static void *load_shim(const char *path, int required) {
  if (!exists(path)) {
    if (required) fprintf(stderr, "[rc] shim not found: %s\n", path);
    return 0;
  }
  void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
  if (!h) fprintf(stderr, "[rc] dlopen %s: %s\n", path, dlerror());
  else if (getenv("RC_LOG_UNRESOLVED")) fprintf(stderr, "[rc] shim loaded: %s\n", path);
  return h;
}

// Mods (step 3b of main): a library, or every .dylib in a folder, in name order.
static int dylib_name(const struct dirent *d) {
  size_t n = strlen(d->d_name);
  return d->d_name[0] != '.' && n > 6 && !strcmp(d->d_name + n - 6, ".dylib");
}
static void load_mods(const char *where) {
  struct dirent **names;
  int n = scandir(where, &names, dylib_name, alphasort);
  if (n < 0) {
    if (exists(where)) load_shim(where, 0);
    return;
  }
  for (int i = 0; i < n; i++) {
    char p[1280];
    snprintf(p, sizeof p, "%s/%s", where, names[i]->d_name);
    if (load_shim(p, 0)) fprintf(stderr, "[rc] mod loaded: %s\n", names[i]->d_name);
    free(names[i]);
  }
  free(names);
}

// ---------------------------------------------------------------------------
// The image's ObjC classrefs that point at image-internal classes. The shell's
// classes are not registered with the runtime when the image is mapped, so
// the reference is redirected to the rewrite's class of the same name.
//
// Only MessageBox is reached from engine code (0x10112e6f0: [[MessageBox alloc]
// init] then the instance method ShowAlert:withDescription:, which the
// rewrite in shims/platform defines).

static int fix_objc(void) {
  const struct mach_header_64 *mh = (const void *)(rc_slide + 0x100000000ull);
  if (mh->magic != MH_MAGIC_64) {
    fprintf(stderr, "[rc] image header not mapped\n");
    return -1;
  }
  int patched = 0;
  const uint8_t *lc = (const uint8_t *)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    if (((const struct load_command *)lc)->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const void *)lc;
    const struct section_64 *sc = (const void *)(sg + 1);
    for (uint32_t k = 0; k < sg->nsects; k++) {
      if (strncmp(sc[k].sectname, "__objc_classrefs", 16) && strncmp(sc[k].sectname, "__objc_superrefs", 16)) continue;
      uint64_t *e = (uint64_t *)(sc[k].addr + rc_slide);
      for (size_t j = 0; j < sc[k].size / 8; j++) {
        if (!e[j] || !rc_in_image(e[j])) continue;
        // objc_class: isa, superclass, cache, vtable, bits (-> class_ro_t, name at +24)
        uint64_t bits = ((const uint64_t *)e[j])[4] & ~7ull;
        const char *name = bits ? *(const char **)(bits + 24) : 0;
        Class nc = name ? objc_getClass(name) : 0;
        if (!nc) {
          fprintf(stderr, "[rc] no native class for image class %s (ref 0x%llx)\n", name ? name : "?",
                  (unsigned long long)(sc[k].addr + 8 * j));
          continue;
        }
        e[j] = (uint64_t)(uintptr_t)nc;
        patched++;
      }
    }
  }
  return patched;
}

// --warm-shaders. libRSOpenGL reads RS_GL_BACKEND once when it loads (before main), so when it
// loaded with OpenGL the process starts again with "metal": the same executable path, so the same
// app for Metal's shader cache.
static int warm_shaders(char **argv) {
  if (rsp_mtl_loaded_backend() != RSP_GFX_METAL && rsp_renderer_parse(getenv("RS_GL_BACKEND")) != RSP_GFX_METAL) {
    char exe[4096];
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) == 0) {
      setenv("RS_GL_BACKEND", "metal", 1);
      execv(exe, argv);
    }
    fprintf(stderr, "[rc] --warm-shaders: cannot restart with RS_GL_BACKEND=metal\n");
    return 2;
  }
  const char *e = getenv("RS_METAL_WARM_MAX_SECONDS");
  double max_s = e && *e ? atof(e) : 3600;
  setvbuf(stdout, NULL, _IOLBF, 0);
  return rsp_mtl_warm_only(max_s > 0 ? max_s : 3600, 0.5, stdout);
}

// --renderer-check
static int renderer_check(char **argv) {
  char b[1024];
  int have = bundle_path(b, sizeof b);
  rsp_renderer_out o = rsp_renderer_startup(argv, have ? b : NULL, rsp_mtl_loaded_backend(), rsp_mtl_usable, 0);
  printf("renderer: %s (%s)\n", rsp_renderer_name(o.backend), rsp_renderer_reason_text(o.reason));
  return 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv, char **envp, char **apple) {
  int init_only = 0, bind_only = 0;
  static char *gargv[64];
  int gargc = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--warm-shaders")) return warm_shaders(argv);
    if (!strcmp(argv[i], "--renderer-check")) return renderer_check(argv);
  }
  for (int i = 0; i < argc && gargc < 63; i++) {
    if (i > 0 && !strcmp(argv[i], "--init-only")) init_only = 1;
    else if (i > 0 && !strcmp(argv[i], "--bind-only")) bind_only = 1;
    else gargv[gargc++] = argv[i];
  }
  gargv[gargc] = 0;
  // 0. the renderer, before anything heavy: may start this process again with RS_GL_BACKEND set.
  if (!init_only && !bind_only) {
    char b[1024];
    int have = bundle_path(b, sizeof b);
    rsp_renderer_startup(argv, have ? b : NULL, rsp_mtl_loaded_backend(), rsp_mtl_usable, RSP_RENDERER_SESSION);
  }
  install_crash_handler();
  if (init_only || bind_only) setenv("RC_LOG_UNRESOLVED", "1", 0);
  setenv("RS_STEAM_MODE", "stub", 0);  // offline: the game must never call home
#ifdef RCAPP_STUB
  // build.sh --stub: no image, no translated library; the shell with the stub engine.
  rsp_set_hooks(rsp_stub_hooks());
  (void)init_only; (void)bind_only; (void)envp; (void)apple;
  return rsp_shell_main(argc, (const char **)argv);
#endif

  char bundle[1024] = "", path[1280], img[1280];
  int have_bundle = bundle_path(bundle, sizeof bundle);
  const char *e = getenv("RS_RC_IMAGE");
  if (e && *e) strlcpy(img, e, sizeof img);
  else if (have_bundle) snprintf(img, sizeof img, "%s/Contents/Resources/Rocksmith2014.x86_64", bundle);
  else {
    fprintf(stderr, "[rc] not running from a .app and RS_RC_IMAGE is not set\n");
    return 2;
  }
  if (!exists(img)) {
    fprintf(stderr, "[rc] image not found: %s\n", img);
    return 2;
  }

  // 1. shims first: rc_image resolves non-system imports with dlsym(RTLD_DEFAULT).
  g_phase = "shims";
  const char *ns = getenv("RS_RC_NO_SHIMS");
  if (have_bundle && !(ns && *ns && *ns != '0')) {
    snprintf(path, sizeof path, "%s/Contents/MacOS/libsteam_api.dylib", bundle);
    load_shim(path, 1);
    snprintf(path, sizeof path, "%s/Contents/MacOS/libBink2Macx64.dylib", bundle);
    load_shim(path, 1);
    snprintf(path, sizeof path, "%s/Contents/Frameworks/Cg.framework/Cg", bundle);
    load_shim(path, 1);
  }
  if ((e = getenv("RS_RC_SHIMS")) && *e) {
    char *list = strdup(e), *save = 0;
    for (char *t = strtok_r(list, ":", &save); t; t = strtok_r(0, ":", &save)) load_shim(t, 1);
    free(list);
  }

  // 2. map, rebase, bind
  g_phase = "image_load";
  double t0 = now_s();
  if (rc_image_load(img)) {
    fprintf(stderr, "[rc] rc_image_load(%s) failed\n", img);
    return 2;
  }
  double t1 = now_s();
  if (bind_only) {
    printf("bind-only: image %s loaded in %.3f s; unresolved %llu slots / %llu symbols\n", img, t1 - t0,
           (unsigned long long)rc_stats.unresolved, (unsigned long long)rc_stats.unresolved_syms);
    return 0;
  }

  // 3. fix-ups that need the native shell classes (registered at exe load)
  g_phase = "objc_fixup";
  int fixed = fix_objc();

  // 3b. mods: every .dylib in the mods folder next to the game app (the install's <home>/mods, which a
  // reinstall keeps), or RS_MODS_PATH (a folder or one library); RS_LOAD_MODS=0 skips them. A mod's constructor
  // registers its hooks (rc_hook.h) now that the image is mapped; the game runs them later.
  g_phase = "mods";
  e = getenv("RS_LOAD_MODS");
  if (!(e && *e == '0')) {
    const char *mods = getenv("RS_MODS_PATH");
    if (!(mods && *mods) && have_bundle) {
      snprintf(path, sizeof path, "%s/../../mods", bundle);  // <home>/game/Rocksmith2014.app -> <home>/mods
      mods = path;
    }
    if (mods && *mods) load_mods(mods);
  }

  // 4. initializers, translated, on the main thread's guest context. The
  // x86 initializers take (argc, argv, envp, apple).
  g_phase = "initializers";
  const uint64_t *inits;
  int n = rc_image_inits(&inits);
  const char *tr = getenv("RC_TRACE_INITS");
  int trace = tr && *tr && *tr != '0';
  uint64_t args[4] = {(uint64_t)gargc, (uint64_t)(uintptr_t)gargv, (uint64_t)(uintptr_t)envp,
                      (uint64_t)(uintptr_t)apple};
  for (int i = 0; i < n; i++) {
    g_init_idx = i;
    g_init_fn = inits[i];
    if (trace) fprintf(stderr, "[rc] init %d/%d at 0x%llx\n", i, n, (unsigned long long)(inits[i] - rc_slide));
    rc_call_guest(inits[i], args, 4, NULL, 0, NULL);
  }
  g_init_idx = -1;
  double t2 = now_s();
  if (init_only) {
    Class mb = objc_getClass("MessageBox");
    printf("init-only: MessageBox classref %s, instance ShowAlert:withDescription: %s\n",
           mb && *(uint64_t *)(rc_slide + 0x10186da28ull) == (uint64_t)(uintptr_t)mb ? "-> native class" : "NOT redirected",
           mb && class_getInstanceMethod(mb, sel_registerName("ShowAlert:withDescription:")) ? "present" : "MISSING");
    printf("init-only: image loaded in %.3f s, %d initializers run in %.3f s; classrefs redirected: %d; "
           "unresolved %llu slots / %llu symbols\n",
           t1 - t0, n, t2 - t1, fixed, (unsigned long long)rc_stats.unresolved,
           (unsigned long long)rc_stats.unresolved_syms);
    return 0;
  }

  // 5. the shell, with the translated engine behind the hooks
  g_phase = "shell";
  rsp_set_hooks(rcapp_seam_hooks());
  int r = rsp_shell_main(argc, (const char **)argv);
  g_phase = "exit";
  return r;
}
