// The built-in fixes' part that is one source for both editions (overrides/mods/mods_shared.c), on the two
// runtimes (Makefile): the same object, built as a mod is, beside a 64-bit runtime with the Mac's kind of tables
// and a 32-bit one with Learn & Play's. The tables below are what targets.py generates in a real build, with
// values of the test's own.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef RC_GUEST32
#include "rc32.h"
#include "rc32_hook.h"
#endif
#include "rc_hook.h"
#include "rc_mid.h"

#include "mods_shared.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define CHECK_EQ(a, b) do { uint64_t a_ = (a), b_ = (b); if (a_ != b_) { \
    fprintf(stderr, "FAIL %s:%d: %s = 0x%llx, not 0x%llx\n", __FILE__, __LINE__, #a, (unsigned long long)a_, \
            (unsigned long long)b_); exit(1); } } while (0)

const char rc_target_id[] = "test";

// --- the target's tables: the hook points of the fixes, each game's own convention and field offsets
enum { INTRO, OWNED, NPOINTS };
#define ADDR(k) (0x401000u + 0x100u * (k))
#ifdef RC_GUEST32
const rc_target_value rc_target_offsets[] = {{"boot_intro_playing", 0x2d}, {"boot_intro_state", 0x60},
                                             {"boot_phase", 0x48}, {"boot_phase3_steps", 10}, {"boot_step", 0x28},
                                             {0, 0}};
// stdcall bool(void *boot), and thiscall bool(void *steam, int app)
const rc_hook_name rc_target_hooks[] = {
    {"boot_intro_update", ADDR(INTRO), 1, 0, RC_CC_STDCALL, 'b', 4, 1, {{'p', 0}}},
    {"steam_dlc_owned", ADDR(OWNED), 2, 0, RC_CC_THISCALL, 'b', 4, 2, {{'p', ~1}, {'i', 0}}},
    {0, 0, 0},
};
int rc_hook_code_pops(int index) {
  static const short pops[NPOINTS] = {4, 4};
  return pops[index];
}
#else
const rc_target_value rc_target_offsets[] = {{"boot_intro_playing", 0x39}, {"boot_intro_state", 0x78},
                                             {"boot_phase", 0x58}, {"boot_phase3_steps", 12}, {"boot_step", 0x34},
                                             {0, 0}};
const rc_hook_name rc_target_hooks[] = {{"boot_intro_update", ADDR(INTRO), ADDR(INTRO)},
                                        {"steam_dlc_owned", ADDR(OWNED), ADDR(OWNED)}, {0, 0, 0}};
#endif
const uint64_t rc_hook_points[] = {ADDR(INTRO), ADDR(OWNED), 0};
const int rc_hook_npoints = NPOINTS;
#define OFF(name) ((uint32_t)rc_off(name))

// What is logged for a while: between log_start and log_stop into `logged`; logged_lines counts the lines that are
// exactly `line`
static FILE *log_file;
static int log_stderr;
static char logged[16384];
static void log_start(void) {
  fflush(stderr);
  log_file = tmpfile();
  CHECK(log_file);
  log_stderr = dup(2);
  dup2(fileno(log_file), 2);
}
static void log_stop(void) {
  fflush(stderr);
  dup2(log_stderr, 2);
  close(log_stderr);
  rewind(log_file);
  logged[fread(logged, 1, sizeof logged - 1, log_file)] = 0;
  fclose(log_file);
}
static int logged_lines(const char *line) {
  size_t len = strlen(line);
  int n = 0;
  for (const char *p = logged; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p))
    n += !strncmp(p, line, len) && p[len] == '\n';
  return n;
}

// --- what the runtimes need of an image
#ifdef RC_GUEST32
rc_fn rc32_lookup(uint32_t g) {
  (void)g;
  return 0;
}
int rc32_tls_images(const rc32_tls_image **out) {
  *out = 0;
  return 0;
}
#else
uint64_t rc_slide;
uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0) {
  (void)fn; (void)ints; (void)nint; (void)dbls; (void)ndbl; (void)xmm0;
  return 0;
}
#endif
_Noreturn void rc_fail(const char *what, uint64_t a) {
  fprintf(stderr, "rc_fail %s 0x%llx\n", what, (unsigned long long)a);
  exit(1);
}

static const char *const SWITCHES[] = {"RS_MODS_LOG", "RS_MODS_BYPASS", "RS_MODS_FIXES", "RS_MODS_INTRO",
                                       "RS_MODS_OVERLAY", "RS_MODS_AUTOSAVE", "RS_MODS_MULTIPLAYER",
                                       "RS_MODS_BUDGET_MS", "RS_MODS_FRAME_RATE"};
static void no_switches(void) {
  for (size_t k = 0; k < sizeof SWITCHES / sizeof *SWITCHES; k++) unsetenv(SWITCHES[k]);
}

// The switches: the defaults, what turns one off, and the budget's range
static void switches(void) {
  no_switches();
  rs_mods_reload_config();
  const rs_mods_config *c = rs_mods_config_get();
  CHECK(c == &rs_mods_cfg);
  CHECK(c->bypass && c->fixes && c->intro && c->overlay && c->autosave && c->multiplayer && !c->log);
  CHECK(c->budget_s == 0.050);
  static const char *const off[] = {"0", "no", "No", "false", "False"}, *const on[] = {"1", "yes", "on", "2", ""};
  for (int k = 0; k < 5; k++) {
    setenv("RS_MODS_X", off[k], 1);
    CHECK(!rs_mods_env_on("RS_MODS_X", 1));
    setenv("RS_MODS_X", on[k], 1);
    CHECK(rs_mods_env_on("RS_MODS_X", 1));
  }
  CHECK(!rs_mods_env_on("RS_MODS_X", 0));  // empty, as unset: the default
  unsetenv("RS_MODS_X");
  CHECK(!rs_mods_env_on("RS_MODS_X", 0) && rs_mods_env_on("RS_MODS_X", 1));
  setenv("RS_MODS_INTRO", "0", 1);
  setenv("RS_MODS_BYPASS", "n", 1);
  setenv("RS_MODS_LOG", "1", 1);
  setenv("RS_MODS_BUDGET_MS", "8", 1);
  rs_mods_reload_config();
  CHECK(!c->intro && !c->bypass && c->log && c->fixes && c->overlay && c->autosave && c->multiplayer);
  CHECK(c->budget_s == 0.008);
  setenv("RS_MODS_BUDGET_MS", "0", 1);  // a step a frame, as the original
  rs_mods_reload_config();
  CHECK(c->budget_s == 0);
  static const char *const bad[] = {"-1", "1001", "fast", ""};
  for (int k = 0; k < 4; k++) {
    setenv("RS_MODS_BUDGET_MS", bad[k], 1);
    rs_mods_reload_config();
    CHECK(c->budget_s == 0.050);
  }
}

// The log: one prefix in both editions, and nothing without RS_MODS_LOG
static void log_lines(void) {
  no_switches();
  rs_mods_reload_config();
  log_start();
  rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", 147u, 6u, 0.31);
  log_stop();
  CHECK_EQ(logged[0], 0);
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  log_start();
  rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", 147u, 6u, 0.31);
  log_stop();
  CHECK(!strcmp(logged, "[rs-mods] DLC: 147 archives mounted in 6 frames, 0.31 s\n"));
}

// The start: it reads the switches itself, adds the hooks of the fixes that are here and says what is on. The
// frame rate fixes find none of their points in this test's tables.
static void start(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  setenv("RS_MODS_AUTOSAVE", "0", 1);
  setenv("RS_MODS_BUDGET_MS", "20", 1);
  log_start();
  rs_mods_start();
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] fixes on, bypass on, overlay on, intro skip on, autosave off, budget 20 ms"), 1);
  CHECK_EQ(logged_lines("[rs-mods] frame rate: on, 0 of 8 points"), 1);
  CHECK_EQ(logged_lines("[rs-mods] no intro hook point") + logged_lines("[rs-mods] no DLC ownership hook point"), 0);
  CHECK(!rs_mods_cfg.autosave && rs_mods_cfg.log);
}

// --- a call of a hook point as the game makes it, and the game's function behind it
static const uint32_t RET = 0x12345678;
static rc_ctx *ctx;
static uint8_t *obj;       // the game's object, as the host sees it
static int orig_ran;
static uint32_t orig_arg;  // what the original was given, where its convention has it (the low word)
#ifdef RC_GUEST32
static uint32_t esp0, obj_guest;
static uint32_t rd(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static void wr(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
// stdcall (boot): the argument on the stack, `ret 4`; al = busy
static void o_intro(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  orig_arg = rd(sp + 4);
  c->r[0] = 0xffffff01u;
  c->ret_to = rd(sp);
  c->r[4] = sp + 8;
}
static uint64_t call_intro(void) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  wr(sp, RET);
  wr(sp + 4, obj_guest);
  orig_ran = 0;
  rc_hook_call(INTRO, ctx, o_intro);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], sp + 8);  // the return address and the argument gone, whoever answered
  return ctx->r[0] & 0xff;
}
// thiscall (steam, app): `this` in ecx, the AppID on the stack, `ret 4`; al = owned
static uint32_t orig_app;
static int owned_answer;
static void o_owned(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  orig_arg = (uint32_t)c->r[1];
  orig_app = rd(sp + 4);
  c->r[0] = 0xffffff00u | (uint32_t)owned_answer;
  c->ret_to = rd(sp);
  c->r[4] = sp + 8;
}
static uint64_t call_owned(uint32_t app) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  ctx->r[1] = obj_guest;
  wr(sp, RET);
  wr(sp + 4, app);
  orig_ran = 0;
  rc_hook_call(OWNED, ctx, o_owned);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], sp + 8);
  return ctx->r[0] & 0xff;
}
#define OBJ_AS_GIVEN obj_guest
#else
static uint64_t stack_[16];
static void o_intro(rc_ctx *c) {
  orig_ran++;
  orig_arg = (uint32_t)c->r[7];
  c->r[0] = 1;
  c->r[4] += 8;
}
static uint64_t call_intro(void) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  ctx->r[7] = (uint64_t)(uintptr_t)obj;
  orig_ran = 0;
  rc_hook_call(INTRO, ctx, o_intro);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
  return ctx->r[0] & 0xff;
}
static uint32_t orig_app;
static int owned_answer;
static void o_owned(rc_ctx *c) {
  orig_ran++;
  orig_arg = (uint32_t)c->r[7];
  orig_app = (uint32_t)c->r[6];
  c->r[0] = (uint64_t)owned_answer;
  c->r[4] += 8;
}
static uint64_t call_owned(uint32_t app) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  ctx->r[7] = (uint64_t)(uintptr_t)obj;
  ctx->r[6] = app;
  orig_ran = 0;
  rc_hook_call(OWNED, ctx, o_owned);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
  return ctx->r[0] & 0xff;
}
#define OBJ_AS_GIVEN ((uint32_t)(uintptr_t)obj)
#endif

static uint32_t fld(const char *name) { uint32_t v; memcpy(&v, obj + OFF(name), 4); return v; }
static void set(const char *name, uint32_t v) { memcpy(obj + OFF(name), &v, 4); }
static void boot_at(uint32_t phase, uint32_t state, uint32_t step) {
  memset(obj, 0, 256);
  set("boot_phase", phase);
  set("boot_intro_state", state);
  set("boot_step", step);
  obj[OFF("boot_intro_playing")] = 1;
}

// No intro screens (the hook start() added): the tick as the game has it while the movie's model loads and once
// the intro is over; "not busy" without the tick while the movie would start or play, until phase 3 has done its
// steps; then the game's own skip, and the tick ends the intro.
static void intro(void) {
  const uint32_t steps = OFF("boot_phase3_steps");
  CHECK(rs_mods_cfg.intro);
  static const uint32_t untouched[][2] = {{0, 2}, {1, 3}, {2, 0}, {2, 1}, {3, 1}, {2, 4}, {3, 4}, {3, 5}, {4, 3}};
  for (size_t k = 0; k < sizeof untouched / sizeof *untouched; k++) {
    boot_at(untouched[k][0], untouched[k][1], 0);
    CHECK_EQ(call_intro(), 1);  // the original's answer
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(orig_arg, OBJ_AS_GIVEN);
    CHECK_EQ(fld("boot_intro_state"), untouched[k][1]);
    CHECK_EQ(obj[OFF("boot_intro_playing")], 1);
  }
  for (uint32_t state = 2; state <= 3; state++) {
    boot_at(2, state, steps);  // phase 2: its steps are phase 3's, whatever the counter says
    CHECK_EQ(call_intro(), 0);
    CHECK_EQ(orig_ran, 0);
    CHECK_EQ(fld("boot_intro_state"), state);
    boot_at(3, state, steps - 1);
    CHECK_EQ(call_intro(), 0);
    CHECK_EQ(orig_ran, 0);
    CHECK_EQ(fld("boot_intro_state"), state);
    CHECK_EQ(obj[OFF("boot_intro_playing")], 1);
    boot_at(3, state, steps);  // done: the game's skip, and the original ends the intro
    log_start();
    CHECK_EQ(call_intro(), 1);
    log_stop();
    CHECK_EQ(logged_lines("[rs-mods] intro: skipped"), 1);
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(orig_arg, OBJ_AS_GIVEN);
    CHECK_EQ(fld("boot_intro_state"), 4);
    CHECK_EQ(obj[OFF("boot_intro_playing")], 0);
  }
  rs_mods_cfg.intro = 0;  // RS_MODS_INTRO=0: the intro plays
  boot_at(3, 2, steps);
  CHECK_EQ(call_intro(), 1);
  CHECK_EQ(orig_ran, 1);
  CHECK_EQ(fld("boot_intro_state"), 2);
  boot_at(2, 3, 0);
  CHECK_EQ(call_intro(), 1);
  CHECK_EQ(orig_ran, 1);
  rs_mods_cfg.intro = 1;
}

// Song ownership during a mount: the game's own answer outside the install steps, yes inside them whatever the
// AppID, and the game's again with RS_MODS_BYPASS=0.
static void ownership(void) {
  CHECK(rs_mods_cfg.bypass);
  for (owned_answer = 0; owned_answer <= 1; owned_answer++) {
    CHECK_EQ(call_owned(248750), (uint64_t)owned_answer);
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(orig_arg, OBJ_AS_GIVEN);
    CHECK_EQ(orig_app, 248750);
  }
  owned_answer = 0;
  rs_mods_mounting(1);
  CHECK_EQ(call_owned(248750), 1);
  CHECK_EQ(orig_ran, 0);
  rs_mods_cfg.bypass = 0;
  CHECK_EQ(call_owned(248750), 0);
  CHECK_EQ(orig_ran, 1);
  rs_mods_cfg.bypass = 1;
  rs_mods_mounting(0);
  CHECK_EQ(call_owned(248750), 0);
  CHECK_EQ(orig_ran, 1);
}

int main(void) {
#ifdef RC_GUEST32
  CHECK(!rc32_mem_init());
  ctx = rc32_thread_ctx();
  esp0 = (uint32_t)ctx->r[4];
  obj_guest = rc32_alloc(256);
  obj = RC32_H(obj_guest);
#else
  static rc_ctx c64;
  ctx = &c64;
  obj = calloc(1, 256);
#endif
  switches();
  log_lines();
  start();
  intro();
  ownership();
  printf("mods_shared: ok (%s)\n", rc_guest_ptr_size == 4 ? "32-bit guest" : "64-bit guest");
  return 0;
}
