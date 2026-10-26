// Differential test: the platform layer's original x86_64 functions, run from
// the mapped game image (shims/game/harness/rsimage.c, compiled in read-only),
// against the rewrites in shims/platform/src. x86_64 only (Rosetta).
//
//   rsp_platform_difftest <analysis/cg/Rocksmith2014_x64_plain>
//
// Compared:
//   paths      SetGameRoot 0x1002a5cf0 (four globals, return value, cwd, Tmp/),
//              the nine path getters, the root-copy hooks 0x1002a5de0/0x1002a5e10
//              at every buffer size, efd IsRelativePath 0x10130a9e0
//   display    SetDisplayMode 0x10017a1f0 / IsFullscreen 0x10017a210 /
//              GetWidth 0x10017a310 / GetHeight 0x10017a330 (GRDisplaySettings
//              with no renderer) vs rsp_* over the stub engine; 0x10017a220
//   input      0x10017a080 into a fake MainService_MacOS (its queue vector and
//              recursive mutex) vs rsp_post_input, with focus/game on and off
//   ini        efd's Rocksmith.ini code on a real efd::File after efd::Init:
//              LoadConfig 0x101315840 (read, and create-with-defaults),
//              Load 0x101313ea0 onto arbitrary prior values, Save 0x101315410,
//              on a generated corpus, vs rsp_config_*
// Prints "N passed, M failed"; exits 1 on any mismatch. RSP_DIFF_DUMP=1 dumps
// the first differing ini cases (text and both blocks).
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#include "efd_env.h"
#include "rsimage.h"
#include "rsp_platform.h"

#define G(addr) ((void *)(uintptr_t)((uint64_t)(addr) + rsimage_slide))

static int g_pass, g_fail, g_printed;
#define CHECK(c, ...)                                     \
  do {                                                    \
    if (c) g_pass++;                                      \
    else {                                                \
      g_fail++;                                           \
      if (g_printed++ < 40) {                             \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
        printf(__VA_ARGS__);                              \
        printf("\n");                                     \
      }                                                   \
    }                                                     \
  } while (0)

static sigjmp_buf g_jb;
#define GUARDED(name, stmt)                                                       \
  do {                                                                            \
    rsimage_set_trap_jmp(&g_jb);                                                  \
    if (sigsetjmp(g_jb, 1) == 0) {                                                \
      stmt;                                                                       \
    } else {                                                                      \
      printf("FAIL %s: original called unresolved import %s\n", name, rsimage_last_trap()); \
      g_fail++;                                                                   \
    }                                                                             \
    rsimage_set_trap_jmp(NULL);                                                   \
  } while (0)

static uint32_t g_rng = 12345;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static char g_tmp[512];

// --- paths -----------------------------------------------------------------------
static void test_paths(void) {
  int (*o_is_rel)(const char *) = (int (*)(const char *))G(0x10130a9e0);
  const char *rel[] = {"", "a", "/", "\\", "./x", "../y", "C:\\x", "//net", " /x", "\x80x", "~/q"};
  for (size_t i = 0; i < sizeof rel / sizeof *rel; i++)
    CHECK(((o_is_rel(rel[i]) & 0xff) != 0) == rsp_path_is_relative(rel[i]), "IsRelative(\"%s\")", rel[i]);
  CHECK((o_is_rel(NULL) & 0xff) == 0 && !rsp_path_is_relative(NULL), "IsRelative(NULL)");

  int (*o_root)(const char *) = (int (*)(const char *))G(0x1002a5cf0);
  const char *(*o_get[9])(void);
  const uint64_t get_addr[9] = {0x1002a5ad0, 0x1002a5ae0, 0x1002a5af0, 0x1002a5b00, 0x1002a5b10,
                                0x1002a5b20, 0x1002a5b30, 0x1002a5d80, 0x1002a5dc0};
  const char *(*my_get[9])(void) = {rsp_path_root, rsp_path_data, rsp_path_tmp, rsp_path_data, rsp_path_tmp,
                                    rsp_path_tmp, rsp_path_root, rsp_path_update, rsp_path_root};
  for (int i = 0; i < 9; i++) o_get[i] = (const char *(*)(void))G(get_addr[i]);
  int (*o_copy[2])(char *, int) = {(int (*)(char *, int))G(0x1002a5de0), (int (*)(char *, int))G(0x1002a5e10)};
  const char *(*o_root_ptr)(void) = (const char *(*)(void))G(0x1002a5dd0);

  for (int k = 0; k < 24; k++) {
    char dir[960];
    int len = k < 8 ? 0 : (int)(rnd() % 700);
    if (k == 0) snprintf(dir, sizeof dir, "%s", g_tmp);
    else if (k == 1) snprintf(dir, sizeof dir, "%s/does-not-exist", g_tmp);
    else if (k == 2) snprintf(dir, sizeof dir, "%s/sp ace", g_tmp);
    else if (k == 3) snprintf(dir, sizeof dir, "%s/\xc3\xa4\xc3\xb6", g_tmp);
    else if (k == 4) snprintf(dir, sizeof dir, "%s/", g_tmp);  // trailing slash -> "//"
    else if (k == 5) snprintf(dir, sizeof dir, "relative-%d", k);
    else if (k == 6) snprintf(dir, sizeof dir, "%s", "");
    else {
      int n = snprintf(dir, sizeof dir, "%s/d%d_", g_tmp, k);
      for (int i = 0; i < len && n < 900; i++) dir[n++] = "abcxyz-_.0189"[rnd() % 13];
      dir[n] = 0;
    }
    if (k != 1 && dir[0]) mkdir(dir, 0755);
    char tmpdir[1100];
    snprintf(tmpdir, sizeof tmpdir, "%s/Tmp", dir);
    char cwd_o[1100] = "", cwd_m[1100] = "";
    chdir(g_tmp);
    rmdir(tmpdir);
    errno = 0;
    int ro = 0;
    GUARDED("SetGameRoot", ro = o_root(dir));
    int eo = errno;
    struct stat st;
    int tmp_o = stat(tmpdir, &st) == 0;
    getcwd(cwd_o, sizeof cwd_o);
    chdir(g_tmp);
    rmdir(tmpdir);
    errno = 0;
    int rm = rsp_paths_set_root(dir);
    int em = errno;
    int tmp_m = stat(tmpdir, &st) == 0;
    getcwd(cwd_m, sizeof cwd_m);
    CHECK(ro == rm && (ro == 0 || eo == em), "SetGameRoot(%s) ret %d/%d errno %d/%d", dir, ro, rm, eo, em);
    CHECK(tmp_o == tmp_m && strcmp(cwd_o, cwd_m) == 0, "SetGameRoot(%s) Tmp %d/%d cwd %s / %s", dir, tmp_o,
          tmp_m, cwd_o, cwd_m);
    CHECK(strcmp(G(0x1018ec0c0), rsp_path_root()) == 0, "root %s / %s", (char *)G(0x1018ec0c0), rsp_path_root());
    CHECK(strcmp(G(0x10194f130), rsp_path_tmp()) == 0, "tmp");
    CHECK(strcmp(G(0x10194ed30), rsp_path_data()) == 0, "data");
    CHECK(strcmp(G(0x10194f530), rsp_path_update()) == 0, "update");
    for (int i = 0; i < 9; i++) CHECK(strcmp(o_get[i](), my_get[i]()) == 0, "getter %d (0x%llx)", i, get_addr[i]);
    CHECK(strcmp(o_root_ptr(), rsp_path_root()) == 0, "getter 0x1002a5dd0");
    int rl = (int)strlen(rsp_path_root());
    for (int h = 0; h < 2; h++)
      for (int size = 0; size <= rl + 2; size += (size < 8 || size > rl - 4) ? 1 : 37) {
        char bo[1100], bm[1100];
        memset(bo, 0x55, sizeof bo);
        memset(bm, 0x55, sizeof bm);
        int a = 0, b;
        GUARDED("root copy hook", a = o_copy[h](bo, size) & 0xff);
        b = rsp_path_copy_root(bm, size);
        CHECK(a == b && memcmp(bo, bm, (size_t)size + 1) == 0, "copy hook %d size %d: %d/%d", h, size, a, b);
      }
  }
  chdir(g_tmp);
}

// --- display settings and the input boundary --------------------------------------
static void test_display(void) {
  void (*o_set)(int, int, int) = (void (*)(int, int, int))G(0x10017a1f0);
  int (*o_isfs)(void) = (int (*)(void))G(0x10017a210);
  int (*o_w)(void) = (int (*)(void))G(0x10017a310);
  int (*o_h)(void) = (int (*)(void))G(0x10017a330);
  int (*o_novsync)(void) = (int (*)(void))G(0x10017a220);
  CHECK(*(uint64_t *)G(0x10191d3d0) == 0, "no renderer in the test image");
  rsp_set_hooks(rsp_stub_hooks());
  for (int i = 0; i < 2000; i++) {
    int fs = (int)(rnd() % 4), w = (int)(rnd() % 5000) - 100, h = (int)(rnd() % 3000) - 100;
    if (i % 7 == 0) fs = 0x100 + fs;  // only the low byte travels
    GUARDED("SetDisplayMode", o_set(fs, w, h));
    rsp_set_display_mode(fs, w, h);
    int a = 0, b = 0, c = 0;
    GUARDED("getters", a = o_isfs() & 0xff; b = o_w(); c = o_h());
    CHECK(a == rsp_is_fullscreen() && b == rsp_screen_width() && c == rsp_screen_height(),
          "after SetDisplayMode(%d,%d,%d): fs %d/%d w %d/%d h %d/%d", fs, w, h, a, rsp_is_fullscreen(), b,
          rsp_screen_width(), c, rsp_screen_height());
    if (g_fail > 30) break;
  }
  // 0x10017a220 reads +0x3c of the (guard-initialised) config block.
  uint32_t *blk = G(0x101873e08);
  int nv = o_novsync() & 0xff;  // runs the guarded initialiser
  CHECK(nv == 0 && blk[0x3c / 4] == 0, "no_vsync default");
  uint32_t vals[] = {0, 1, 2, 0xffffffff, 0x100};
  for (size_t i = 0; i < 5; i++) {
    blk[0x3c / 4] = vals[i];
    rsp_config_global()->no_vsync = vals[i];
    rsp_stub()->no_vsync = (int)vals[i];
    CHECK((o_novsync() & 0xff) == rsp_no_vsync(), "no_vsync %u", vals[i]);
  }
  blk[0x3c / 4] = 0;
  rsp_stub()->no_vsync = 0;
}

typedef struct { int code, a, b; } rec;
static rec g_rec[8192];
static int g_nrec;
static void rec_input(void *u, int code, int a, int b) {
  if (g_nrec < 8192) g_rec[g_nrec++] = (rec){code, a, b};
}

static void test_input(void) {
  void (*o_post)(int, int, int) = (void (*)(int, int, int))G(0x10017a080);
  // A fake MainService_MacOS: only the queue (+0xa8 begin, +0xb0 end, +0xb8
  // cap), its recursive mutex (+0xc0), owner (+0x100) and depth (+0x108).
  uint8_t *fake = calloc(1, 0x200);
  pthread_mutexattr_t at;
  pthread_mutexattr_init(&at);
  pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init((pthread_mutex_t *)(fake + 0xc0), &at);
  uint64_t *game = G(0x1018e7470);
  uint8_t *focus = G(0x10194b391);
  uint64_t saved = *game;

  rsp_engine_hooks h = *rsp_stub_hooks();
  rsp_set_hooks(&h);
  rsp_stub()->input_cb = rec_input;
  rsp_input_reset();
  g_nrec = 0;
  int sent = 0;
  for (int i = 0; i < 3000; i++) {
    int present = (i / 500) % 3 != 2, foc = (rnd() % 5) != 0;
    int type = (int)(rnd() % 7);
    if (rnd() % 20 == 0) type = (int)rnd();  // odd types (not 6/7: those hit the audio engine)
    if (type == 6 || type == 7) type = 8;
    int a = (int)rnd(), b = (int)rnd();
    *game = present ? (uint64_t)(uintptr_t)fake : 0;
    *focus = (uint8_t)foc;
    rsp_game_set_present(present);
    // the rewrite's focus byte is private: drive it through rsp_set_focus with
    // hooks that do nothing
    h.app_active = NULL;
    h.focus_lost = NULL;
    rsp_set_hooks(&h);
    rsp_set_focus(foc);
    GUARDED("0x10017a080", o_post(type, a, b));
    rsp_post_input(type, a, b);
    sent += present && foc;
  }
  *game = saved;
  *focus = 0;
  int32_t *beg = *(int32_t **)(fake + 0xa8), *end = *(int32_t **)(fake + 0xb0);
  size_t n = (size_t)(end - beg) / 3;
  rsp_input_flush();
  CHECK((int)n == sent && g_nrec == sent, "queued %zu original / %d rewrite / %d expected", n, g_nrec, sent);
  for (size_t i = 0; i < n && i < (size_t)g_nrec; i++)
    CHECK(rsp_gfx_event_code(beg[3 * i]) == g_rec[i].code && beg[3 * i + 1] == g_rec[i].a &&
              beg[3 * i + 2] == g_rec[i].b,
          "event %zu: {%d,%d,%d} vs {gfx %d,%d,%d}", i, beg[3 * i], beg[3 * i + 1], beg[3 * i + 2], g_rec[i].code,
          g_rec[i].a, g_rec[i].b);
  CHECK(*(int32_t *)(fake + 0x108) == 0, "mutex depth back to 0");
  rsp_stub()->input_cb = NULL;
  rsp_set_hooks(rsp_stub_hooks());
}

// --- Rocksmith.ini ---------------------------------------------------------------
static char g_ini[1100];

static void write_file(const char *path, const char *p, size_t n) {
  FILE *f = fopen(path, "wb");
  fwrite(p, 1, n, f);
  fclose(f);
}
static size_t read_file(const char *path, char *buf, size_t size) {
  FILE *f = fopen(path, "rb");
  if (!f) return (size_t)-1;
  size_t n = fread(buf, 1, size, f);
  fclose(f);
  return n;
}

static const char *k_keys[] = {"EnableMicrophone", "LatencyBuffer", "DumpAudioLog", "RealToneCableOnly",
                               "MonoToStereoChannel", "ScreenWidth", "ScreenHeight", "Fullscreen",
                               "VisualQuality", "RenderingWidth", "RenderingHeight", "EnablePostEffects",
                               "EnableShadows", "EnableHighResScope", "EnableDepthOfField",
                               "EnablePerPixelLighting", "MsaaSamples", "DisableBrowser", "UseProxy",
                               "AutoLogin", "Version"};
static const char *k_sections[] = {"[Audio]", "[Renderer.MacOS]", "[Net]", "[Global]"};

// A random ini: real sections and keys in random order and number, with noise.
static size_t gen_ini(char *out, size_t cap) {
  size_t n = 0;
#define EMIT(...) n += (size_t)snprintf(out + n, cap - n, __VA_ARGS__)
  int lines = 1 + (int)(rnd() % 40);
  const char *nl = rnd() % 4 == 0 ? "\r\n" : "\n";
  for (int i = 0; i < lines && n < cap - 300; i++) {
    uint32_t r = rnd() % 100;
    if (r < 18) EMIT("%s%s", k_sections[rnd() % 4], nl);
    else if (r < 80) {
      const char *k = k_keys[rnd() % 21];
      uint32_t v = rnd();
      switch (rnd() % 10) {
        case 0: EMIT("%s=0x%x%s", k, v % 5000, nl); break;
        case 1: EMIT("%s=0%o%s", k, v % 512, nl); break;
        case 2: EMIT("%s=-%u%s", k, v % 9, nl); break;
        case 3: EMIT("%s= %u%s", k, v % 4, nl); break;
        case 4: EMIT("%s=%u junk%s", k, v % 3000, nl); break;
        case 5: EMIT("%s=%llu%s", k, (unsigned long long)v * 4294967311ull, nl); break;
        case 6: EMIT("%s%s", k, nl); break;
        default: EMIT("%s=%u%s", k, rnd() % 3 == 0 ? v % 4 : v % 2000, nl); break;
      }
    } else if (r < 85) EMIT("%s", nl);
    else if (r < 88) EMIT(" %s=1%s", k_keys[rnd() % 21], nl);
    else if (r < 91) EMIT("[%s%s", k_keys[rnd() % 21], nl);
    else if (r < 93) EMIT("%s %s", k_sections[rnd() % 4], nl);
    else if (r < 95) {
      int len = 0xff0 + (int)(rnd() % 32);
      for (int j = 0; j < len && n < cap - 300; j++) out[n++] = 'x';
      EMIT("%s=%u%s", k_keys[rnd() % 21], rnd() % 10, nl);
    } else if (r < 97) {
      out[n++] = 0;
      EMIT("%s=%u%s", k_keys[rnd() % 21], rnd() % 10, nl);
    } else EMIT("; comment %u%s", rnd(), nl);
  }
  if (rnd() % 5 == 0 && n > 0 && out[n - 1] == '\n') n--;  // no trailing newline
#undef EMIT
  return n;
}

static void random_config(rsp_config *c) {
  rsp_config_defaults(c);
  uint8_t *p = (uint8_t *)c;
  for (int i = 0; i < 0x4c; i++)
    if (rnd() % 3 == 0) p[i] = (uint8_t)rnd();
  // the byte flags are bools in practice; keep a few odd ones
  if (rnd() % 4) c->enable_microphone &= 1, c->use_proxy &= 1, c->auto_login &= 1, c->disable_browser &= 1;
  c->pad3 = 0;
  c->pad49[0] = c->pad49[1] = c->pad49[2] = 0;
}

static void test_ini(void) {
  // The game's own wiring: SetGameRoot, then efd's path hooks pointed at the
  // game's root-copy functions (0x100179dc0 does exactly this).
  int (*o_root)(const char *) = (int (*)(const char *))G(0x1002a5cf0);
  void (*o_hooks)(void *, void *, void *, void *, void *, void *) =
      (void (*)(void *, void *, void *, void *, void *, void *))G(0x1012f6ce0);
  void (*o_loadcfg)(void) = (void (*)(void))G(0x101315840);
  int (*o_load)(rsp_config *) = (int (*)(rsp_config *))G(0x101313ea0);
  int (*o_save)(const rsp_config *) = (int (*)(const rsp_config *))G(0x101315410);
  char dir[700];
  snprintf(dir, sizeof dir, "%s/ini", g_tmp);
  mkdir(dir, 0755);
  GUARDED("SetGameRoot", o_root(dir));
  rsp_paths_set_root(dir);
  o_hooks(NULL, NULL, G(0x1002a5de0), G(0x1002a5dc0), G(0x1002a5e10), G(0x1002a5dd0));
  rsp_path_ini(g_ini, sizeof g_ini);
  uint8_t *blk = G(0x101873e08), *guard = G(0x101873e58);

  // 1. No file: both write the defaults.
  unlink(g_ini);
  GUARDED("LoadConfig (create)", o_loadcfg());
  static char fo[0x8000], fm[0x8000];
  size_t no = read_file(g_ini, fo, sizeof fo);
  unlink(g_ini);
  rsp_config c;
  rsp_config_defaults(&c);
  rsp_config_load_or_create(&c);
  size_t nm = read_file(g_ini, fm, sizeof fm);
  CHECK(no != (size_t)-1 && no == nm && memcmp(fo, fm, no) == 0, "created defaults file (%zu/%zu bytes)", no, nm);

  // 2. LoadConfig on a corpus: the static block after the original vs the rewrite.
  static char text[0x6000];
  int cases = 0;
  const char *fixed[] = {
      "[Audio]\nEnableMicrophone=0\nLatencyBuffer=2\nDumpAudioLog=0\nRealToneCableOnly=0\n"
      "MonoToStereoChannel=0\n[Renderer.MacOS]\nScreenWidth=0\nScreenHeight=0\nFullscreen=0\n"
      "VisualQuality=3\nRenderingWidth=0\nRenderingHeight=0\nEnablePostEffects=1\nEnableShadows=1\n"
      "EnableHighResScope=0\nEnableDepthOfField=1\nEnablePerPixelLighting=1\nMsaaSamples=4\n"
      "DisableBrowser=0\n[Net]\nUseProxy=1\n[Global]\nVersion=1\n",
      "", "\n", "[Audio]", "[Renderer.MacOS]\nFullscreen=1\n", "[Renderer.MacOS]\nFullscreen=3\nVisualQuality=4\n",
      "[Renderer.MacOS]\nFullscreen=4294967295\n", "[Global]\nVersion=0\n", "[Global]\nVersion=7\n",
      "[Net]\nAutoLogin=0\nUseProxy=2\n", "[Audio]\r\nLatencyBuffer=0x10\r\n"};
  for (int i = 0; i < 1500; i++) {
    size_t n;
    if (i < (int)(sizeof fixed / sizeof *fixed)) n = strlen(memcpy(text, fixed[i], strlen(fixed[i]) + 1));
    else n = gen_ini(text, sizeof text);
    write_file(g_ini, text, n);
    if (getenv("RSP_DIFF_DUMP") && i < 6) printf("case %d guard %016llx lat %u\n", i, *(unsigned long long *)guard, *(uint32_t *)(blk + 4));
    memset(guard, 0, 8);  // re-run the block's guarded initialiser (the whole guard word)
    GUARDED("LoadConfig", o_loadcfg());
    rsp_config_defaults(&c);
    rsp_config_load_or_create(&c);
    CHECK(memcmp(blk, &c, sizeof c) == 0, "LoadConfig case %d differs (first byte %ld)", i,
          (long)({
            long k = 0;
            while (k < (long)sizeof c && blk[k] == ((uint8_t *)&c)[k]) k++;
            k;
          }));
    if (memcmp(blk, &c, sizeof c) != 0 && getenv("RSP_DIFF_DUMP")) {
      printf("case %d text:\n%.*s\n--\n", i, (int)n, text);
      for (int k = 0; k < (int)sizeof c; k += 4)
        printf("+%02x %08x %08x%s\n", k, *(uint32_t *)(blk + k), *(uint32_t *)((uint8_t *)&c + k),
               *(uint32_t *)(blk + k) != *(uint32_t *)((uint8_t *)&c + k) ? "  <--" : "");
    }
    cases++;
    if (g_fail > 30) break;
  }

  // 3. Load onto arbitrary prior values: absent keys keep them.
  for (int i = 0; i < 500; i++) {
    size_t n = gen_ini(text, sizeof text);
    write_file(g_ini, text, n);
    rsp_config prior, a, b;
    random_config(&prior);
    a = prior;
    b = prior;
    int r = 0;
    GUARDED("Load", r = o_load(&a) & 0xff);
    rsp_config_parse(&b, text, n);
    CHECK(r == 1 && memcmp(&a, &b, sizeof a) == 0, "Load case %d differs", i);
    if (g_fail > 30) break;
  }

  // 4. Save: the bytes written.
  for (int i = 0; i < 500; i++) {
    rsp_config a;
    random_config(&a);
    unlink(g_ini);
    int r = 0;
    GUARDED("Save", r = o_save(&a) & 0xff);
    no = read_file(g_ini, fo, sizeof fo);
    nm = rsp_config_write(&a, fm, sizeof fm);
    CHECK(r == 1 && no == nm && memcmp(fo, fm, no) == 0, "Save case %d differs (%zu/%zu)", i, no, nm);
    if (g_fail > 30) break;
  }
  unlink(g_ini);
  printf("ini: %d corpus files\n", cases);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <Rocksmith2014_x64_plain>\n", argv[0]);
    return 2;
  }
  setenv("RSP_LOG", "0", 0);
  rsimage_info info;
  if (rsimage_load(argv[1], &info) != 0) return 2;
  snprintf(g_tmp, sizeof g_tmp, "%s/rsp_difftest.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  for (char *p = g_tmp; (p = strstr(p, "//"));) memmove(p, p + 1, strlen(p));
  if (!mkdtemp(g_tmp)) return 2;
  char *real = realpath(g_tmp, NULL);
  snprintf(g_tmp, sizeof g_tmp, "%s", real);
  free(real);

  test_paths();
  int n = efd_env_start();
  if (n < 0) {
    printf("FAIL efd_env_start\n");
    g_fail++;
  } else {
    test_display();
    test_input();
    test_ini();
  }
  printf("rsp_platform_difftest: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
