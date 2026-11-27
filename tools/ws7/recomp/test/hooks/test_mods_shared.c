// The built-in fixes' part that is one source for both editions (overrides/mods/mods_shared.c), on the two
// runtimes (Makefile): the same object, built as a mod is, beside a 64-bit runtime with the Mac's kind of tables
// and a 32-bit one with Learn & Play's. The tables below are what targets.py generates in a real build, with
// values of the test's own.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef RC_GUEST32
#include "rc32.h"
#include "rc32_hook.h"
#endif
#include "rc_cc.h"
#include "rc_hook.h"
#include "rc_mid.h"

#include "mods_shared.h"

static void log_end(void);  // a failure while the log is captured is said on the real stderr
#define CHECK(x) do { if (!(x)) { log_end(); \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define CHECK_EQ(a, b) do { uint64_t a_ = (a), b_ = (b); if (a_ != b_) { log_end(); \
    fprintf(stderr, "FAIL %s:%d: %s = 0x%llx, not 0x%llx\n", __FILE__, __LINE__, #a, (unsigned long long)a_, \
            (unsigned long long)b_); exit(1); } } while (0)

const char rc_target_id[] = "test";

// --- the target's tables: the hook points of the fixes, each game's own convention and field offsets, and what
// the fixes call and read in the game by name: its functions at ADDR, its data at DATA
enum { INTRO, OWNED, BIND, FRAME, NPOINTS, MODE = 8, PROFILE, MANAGER, BYNAME, DESTROY, FSEXISTS = 14, MIDS = 16,
       REQUEST = 24, G_FIND = 32, G_REMOVE, G_MERGE, G_SET, G_KEY, G_BUILD, G_FUP, G_FDOWN, G_DTOR, G_ADDREF, G_RELEASE,
       G_CBUILD };
#define ADDR(k) (0x401000u + 0x100u * (k))
#define DATA 0x402000u
enum { D_BINDING = 0x10, D_KEY_DIRECT = 0x40, D_KEY_MIC = 0x50, D_TEXT_DIRECT = 0x80, D_TEXT_MIC = 0xa0,
       D_LAS_VTABLE = 0xc0, D_OTHER_VTABLE = 0xd0, D_NODE_VTABLE = 0xe0, D_COOP_VTABLE = 0xf0, D_SIZE = 0x100 };
#ifdef RC_GUEST32
const rc_target_value rc_target_offsets[] = {{"asset_load_name", 0x10}, {"boot_intro_playing", 0x2d},
                                             {"boot_intro_state", 0x60}, {"boot_phase", 0x48},
                                             {"boot_phase3_steps", 10}, {"boot_step", 0x28}, {"efd_str_inline", 0x14},
                                             {"efd_str_size", 0x10}, {"file_index_name", -0x38}, {"fs_exists_slot", 6},
                                             {"input_slot", 0xc}, {"menu_movie_uri", 0x8c}, {"json_node_add_ref_slot", 0},
                                             {"json_node_release_slot", 1}, {"menu_page_root", 0x1a0},
                                             {"menu_page_node", 0x1c4}, {"menu_page_focus", 0x1fc},
                                             {"menu_page_focus_lists", 0x214}, {"las_page_records", 0x388},
                                             {"las_page_records2", 0x394}, {"las_page_selected", 0x3a0},
                                             {"las_page_slot_counter", 0x3a4}, {"arr_record_size", 0xfc},
                                             {"arr_coop_records", 0x370}, {"arr_coop_selected", 0x37c}, {0, 0}};
// stdcall bool(void *boot), thiscall bool(void *steam, int app), and custom void(int input@eax, int force=0)
const rc_hook_name rc_target_hooks[] = {
    {"boot_intro_update", ADDR(INTRO), 1, 0, RC_CC_STDCALL, 'b', 4, 1, {{'p', 0}}},
    {"steam_dlc_owned", ADDR(OWNED), 2, 0, RC_CC_THISCALL, 'b', 4, 2, {{'p', ~1}, {'i', 0}}},
    {"input_bind", ADDR(BIND), 3, 0, RC_CC_CUSTOM, 'v', -1, 2, {{'i', ~0}, {'c', 0, 0}}},
    {"game_frame", ADDR(FRAME), 4, 0, RC_CC_CDECL, 'b', 0, 0, {{0}}},  // (the real one hides its manager argument)
    {0, 0, 0},
};
int rc_hook_code_pops(int index) {
  static const short pops[NPOINTS] = {4, 4, 0, 0};
  return pops[index];
}
#define CC(cc) RC_CC_##cc
#else
const rc_target_value rc_target_offsets[] = {{"asset_load_name", 0x18}, {"boot_intro_playing", 0x39},
                                             {"boot_intro_state", 0x78}, {"boot_phase", 0x58},
                                             {"boot_phase3_steps", 12}, {"boot_step", 0x34}, {"efd_str_inline", 0x18},
                                             {"efd_str_size", 0x10}, {"file_index_name", -0x498}, {"fs_exists_slot", 7},
                                             {"input_slot", 0x14}, {"menu_movie_uri", 0xe0}, {"json_node_add_ref_slot", 0},
                                             {"json_node_release_slot", 1}, {"menu_page_root", 0x250},
                                             {"menu_page_node", 0x290}, {"menu_page_focus", 0x2e8},
                                             {"menu_page_focus_lists", 0x308}, {"las_page_records", 0x500},
                                             {"las_page_records2", 0x518}, {"las_page_selected", 0x530},
                                             {"las_page_slot_counter", 0x534}, {"arr_record_size", 0x180},
                                             {"arr_coop_records", 0x4e0}, {"arr_coop_selected", 0x4f8}, {0, 0}};
const rc_hook_name rc_target_hooks[] = {{"boot_intro_update", ADDR(INTRO), ADDR(INTRO)},
                                        {"steam_dlc_owned", ADDR(OWNED), ADDR(OWNED)},
                                        {"input_bind", ADDR(BIND), ADDR(BIND)},
                                        {"game_frame", ADDR(FRAME), ADDR(FRAME)}, {0, 0, 0}};
#define CC(cc) RC_CC_SYSV
#endif
const rc_target_value rc_target_symbols[] = {
    {"efd_str_destroy", ADDR(DESTROY), CC(THISCALL)},
    {"input_binding", DATA + D_BINDING, -1},     {"input_by_name", ADDR(BYNAME), CC(STDCALL)},
    {"input_key_direct", DATA + D_KEY_DIRECT, -1}, {"input_key_mic", DATA + D_KEY_MIC, -1},
    {"input_manager", ADDR(MANAGER), CC(CDECL)}, {"input_mode", ADDR(MODE), CC(CDECL)},
    {"input_profile_string", ADDR(PROFILE), CC(CDECL)},
#ifdef RC_GUEST32
    {"menu_movie_request_reg", ADDR(REQUEST), RC_CC_CUSTOM},  // the movie in eax, the id on the stack, `ret 8`
    {"json_find_ecx", ADDR(G_FIND), RC_CC_THISCALL},          // (node), the name in eax
    {"json_key_intern", ADDR(G_KEY), RC_CC_THISCALL},         // (text, &out)
#else
    {"menu_movie_request", ADDR(REQUEST), RC_CC_SYSV},  // (movie, a pointer to the id's words)
    {"json_find", ADDR(G_FIND), RC_CC_SYSV},
    {"json_key_make", ADDR(G_KEY), RC_CC_SYSV},         // (&out, text)
#endif
    {"json_object_remove", ADDR(G_REMOVE), CC(STDCALL)}, {"json_object_merge", ADDR(G_MERGE), CC(THISCALL)},
    {"json_object_set", ADDR(G_SET), CC(STDCALL)},       {"las_build", ADDR(G_BUILD), CC(THISCALL)},
    {"las_page_vtable", DATA + D_LAS_VTABLE, -1},        {"menu_focus_up", ADDR(G_FUP), CC(THISCALL)},
    {"menu_focus_down", ADDR(G_FDOWN), CC(THISCALL)},    {"arr_record_dtor", ADDR(G_DTOR), CC(THISCALL)},
    {"arr_coop_vtable", DATA + D_COOP_VTABLE, -1},       {"arr_coop_build", ADDR(G_CBUILD), CC(THISCALL)},
    {0, 0, 0}};
const uint64_t rc_hook_points[] = {ADDR(INTRO), ADDR(OWNED), ADDR(BIND), ADDR(FRAME), 0};
const int rc_hook_npoints = NPOINTS;
// The mid points the fixes here hook, with their values where each game has them. Skip Tuner's answer: the low
// byte of a register, a local below the frame pointer. The batched mounting's four: registers on the Mac; in
// Learn & Play an argument above the frame pointer too, and at the file question the path and the answer in one
// register, the game's call not yet made.
#define SKIP_LOCAL (-0x1d)
enum { M_SKIP, M_REQUEST, M_RESPONSE, M_MOUNT, M_EXISTS, M_ASKED, M_LOCATE, M_LOCATED, M_INDEXED, M_ALT, M_LABEL, M_ROOT,
       M_OPEN, M_FUP, M_FDOWN, M_CSORT, M_CSEL };
#ifdef RC_GUEST32
const rc_mid_name rc_target_mids[] = {
    {"tuner_skip_offer", ADDR(DESTROY + 1), 0, 1, {{"offer", 'b', RC_MID_MEM | 5, SKIP_LOCAL}}, 0, {0}, 0},
    {"manifest_request", ADDR(MIDS), 0, 1, {{"request", 'p', 3, 0}}, 0, {0}, 0},
    {"manifest_response", ADDR(MIDS + 1), 0, 2, {{"manager", 'p', 1, 0}, {"response", 'p', RC_MID_MEM | 5, 8}}, 0,
     {0}, 0},
    {"fs_mount", ADDR(MIDS + 2), 0, 2, {{"path", 'p', RC_MID_MEM | 5, 8}, {"fs", 'p', 0, 0}}, 0, {0}, 0},
    {"fs_exists", ADDR(MIDS + 3), 0, 3, {{"fs", 'p', 1, 0}, {"path", 'p', 0, 0}, {"found", 'b', 0, 0}}, 1,
     {"answered"}, 0},
    {"menu_request", ADDR(MIDS + 4), 0, 3,
     {{"movie", 'p', 6, 0}, {"id_lo", 'i', RC_MID_MEM | 5, 8}, {"id_hi", 'i', RC_MID_MEM | 5, 12}}, 0, {0}, 0},
    {"menu_locate", ADDR(MIDS + 5), 0, 0, {{0}}, 0, {0}, 0},
    {"menu_located", ADDR(MIDS + 6), 0, 1, {{"accepted", 'b', 0, 0}}, 0, {0}, 0},
    {"file_indexed", ADDR(MIDS + 7), 0, 1, {{"frame", 'p', 5, 0}}, 0, {0}, 0},
    {"las_alt_slot", ADDR(MIDS + 8), 0, 3,
     {{"filled", 'i', RC_MID_MEM | 4, 0x6c}, {"page", 'p', 3, 0}, {"record", 'i', RC_MID_MEM | 4, 0x44}}, 1, {"latch"}, 0},
    {"las_alt_label", ADDR(MIDS + 9), 0, 1, {{"text", 'p', 7, 0}}, 0, {0}, 0},
    {"las_alt_root", ADDR(MIDS + 10), 0, 1, {{"page", 'p', 3, 0}}, 0, {0}, 0},
    {"las_alt_page", ADDR(MIDS + 11), 0, 1, {{"page", 'p', 7, 0}}, 0, {0}, 0},
    {"menu_focus_step_up", ADDR(MIDS + 12), 0, 2, {{"page", 'p', 7, 0}, {"steps", 'i', RC_MID_MEM | 5, 8}}, 0, {0}, 0},
    {"menu_focus_step_down", ADDR(MIDS + 13), 0, 2, {{"page", 'p', 7, 0}, {"steps", 'i', RC_MID_MEM | 5, 8}}, 0, {0}, 0},
    {"coop_sort_order", ADDR(MIDS + 14), 0, 3,
     {{"row", 'd', 32, 0}, {"record", 'i', RC_MID_MEM | 4, 0x38}, {"page", 'p', RC_MID_MEM | 4, 0x18}}, 0, {0}, 0},
    {"coop_select", ADDR(MIDS + 15), 0, 2, {{"record", 'i', 1, 0}, {"page", 'p', 3, 0}}, 0, {0}, 0},
    {0}};
#else
const rc_mid_name rc_target_mids[] = {
    {"tuner_skip_offer", ADDR(DESTROY + 1), 0, 1, {{"offer", 'b', 14, 0}}, 0, {0}, 0},
    {"manifest_request", ADDR(MIDS), 0, 1, {{"request", 'p', 13, 0}}, 0, {0}, 0},
    {"manifest_response", ADDR(MIDS + 1), 0, 2, {{"manager", 'p', 7, 0}, {"response", 'p', 6, 0}}, 0, {0}, 0},
    {"fs_mount", ADDR(MIDS + 2), 0, 2, {{"path", 'p', 7, 0}, {"fs", 'p', 6, 0}}, 0, {0}, 0},
    {"fs_exists", ADDR(MIDS + 3), 0, 3, {{"fs", 'p', 7, 0}, {"path", 'p', 6, 0}, {"found", 'b', 0, 0}}, 1,
     {"answered"}, 0},
    {"menu_request", ADDR(MIDS + 4), 0, 3,
     {{"movie", 'p', 7, 0}, {"id_lo", 'i', RC_MID_MEM | 6, 0}, {"id_hi", 'i', RC_MID_MEM | 6, 4}}, 0, {0}, 0},
    {"menu_locate", ADDR(MIDS + 5), 0, 0, {{0}}, 0, {0}, 0},
    {"menu_located", ADDR(MIDS + 6), 0, 1, {{"accepted", 'b', 0, 0}}, 0, {0}, 0},
    {"file_indexed", ADDR(MIDS + 7), 0, 1, {{"frame", 'p', 5, 0}}, 0, {0}, 0},
    {"las_alt_slot", ADDR(MIDS + 8), 0, 3,
     {{"filled", 'i', RC_MID_MEM | 5, -0x348}, {"page", 'p', 12, 0}, {"record", 'q', 13, 0}}, 1, {"latch"}, 0},
    {"las_alt_label", ADDR(MIDS + 9), 0, 1, {{"text", 'p', 1, 0}}, 0, {0}, 0},
    {"las_alt_root", ADDR(MIDS + 10), 0, 1, {{"page", 'p', 15, 0}}, 0, {0}, 0},
    {"las_alt_page", ADDR(MIDS + 11), 0, 1, {{"page", 'p', 13, 0}}, 0, {0}, 0},
    {"menu_focus_step_up", ADDR(MIDS + 12), 0, 2, {{"page", 'p', 3, 0}, {"steps", 'i', 12, 0}}, 0, {0}, 0},
    {"menu_focus_step_down", ADDR(MIDS + 13), 0, 2, {{"page", 'p', 13, 0}, {"steps", 'i', 12, 0}}, 0, {0}, 0},
    {"coop_sort_order", ADDR(MIDS + 14), 0, 3,
     {{"row", 'd', 16, 0}, {"record", 'i', 13, 0}, {"page", 'p', RC_MID_MEM | 5, -0x2e8}}, 0, {0}, 0},
    {"coop_select", ADDR(MIDS + 15), 0, 2, {{"record", 'i', 0, 0}, {"page", 'p', 14, 0}}, 0, {0}, 0},
    {0}};
#endif
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
static void log_end(void) {
  if (!log_file) return;
  fflush(stderr);
  dup2(log_stderr, 2);
  close(log_stderr);
}
static void log_stop(void) {
  log_end();
  rewind(log_file);
  logged[fread(logged, 1, sizeof logged - 1, log_file)] = 0;
  fclose(log_file);
  log_file = 0;
}
static int logged_lines(const char *line) {
  size_t len = strlen(line);
  int n = 0;
  for (const char *p = logged; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p))
    n += !strncmp(p, line, len) && p[len] == '\n';
  return n;
}

_Noreturn void rc_fail(const char *what, uint64_t a) {
  fprintf(stderr, "rc_fail %s 0x%llx\n", what, (unsigned long long)a);
  exit(1);
}

// --- the game's side of Player 2's saved input: what the fix calls and reads, as each game has it. A pointer
// the game holds is its own width, and what it is handed has to be its own memory.
static uint8_t *data;  // DATA as the host sees it
#ifdef RC_GUEST32
static uint64_t as_game(const void *h) { return h ? RC32_G(h) : 0; }
static void *as_host(uint64_t g) { return g ? RC32_H((uint32_t)g) : 0; }
static void *game_alloc(uint32_t size) { return RC32_H(rc32_alloc(size)); }
#else
uint64_t rc_slide;
static uint64_t as_game(const void *h) { return (uint64_t)(uintptr_t)h; }
static void *as_host(uint64_t g) { return (void *)(uintptr_t)g; }
static void *game_alloc(uint32_t size) { return calloc(1, size); }
#endif
static void put_ptr(uint8_t *at, const void *h) {
  uint64_t g = as_game(h);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}
static void *get_ptr(const uint8_t *at) {
  uint64_t g = 0;
  memcpy(&g, at, (size_t)rc_guest_ptr_size);
  return as_host(g);
}

static int game_mode_is, mode_calls, profile_calls, manager_calls, by_name_calls;
static uint32_t mode_player, profile_player;
static const char *profile_key;   // the key the profile was asked for
static const char *profile_text;  // what the profile has under it; null: a string with no text at all
static char by_name_seen[600];
#define TEXT_HEAP 512
static const char *input_name;    // the one input the manager has, and the object it is
static uint8_t *input_obj, *manager_obj, *text_heap;
static uint8_t *profile_out;      // the string the profile's text was last written into
static int text_live, text_frees, destroy_calls;  // text_heap holds a text the game has not freed; frees of it

// input_mode(player) -> al, the rest of eax whatever it was
static uint32_t game_mode(uint32_t player) {
  mode_calls++;
  mode_player = player;
  return 0xffffff00u | (uint32_t)game_mode_is;
}
// input_profile_string(player, key, string *out): the game's small string, given empty; a text that fits before
// its size word goes in place, a longer one behind a pointer at its start
static void game_profile_string(uint32_t player, const char *key, uint8_t *out) {
  uint8_t *end = out + rc_off("efd_str_size"), *in_place = out + rc_off("efd_str_inline");
  size_t n = profile_text ? strlen(profile_text) : 0;
  profile_calls++;
  profile_player = player;
  profile_key = key;
  profile_out = out;
  CHECK(key && out);  // null: the runtime could not hand the game what it was given
  CHECK(get_ptr(end) == out && get_ptr(in_place) == end);
  CHECK(n < TEXT_HEAP);
  CHECK(!text_live);  // the text of the call before was never freed
  if (!profile_text) {
    put_ptr(out, 0);
    put_ptr(end, 0);
    put_ptr(in_place, 0);
  } else if (n < (size_t)rc_off("efd_str_size")) {
    memcpy(out, profile_text, n);
    put_ptr(end, out + n);
  } else {
    memcpy(text_heap, profile_text, n);
    put_ptr(out, text_heap);
    put_ptr(end, text_heap + n);
    put_ptr(in_place, text_heap + n);  // the end of what was allocated
    text_live = 1;
  }
}
// efd_str_destroy(string *): frees the text unless it is in place, as the game's own callers do once they have
// read the string; what was there is gone
static void game_str_destroy(uint8_t *s) {
  uint8_t *end = s + rc_off("efd_str_size"), *in_place = s + rc_off("efd_str_inline");
  destroy_calls++;
  CHECK(s && s == profile_out);
  if (get_ptr(in_place) == end || !get_ptr(s)) return;
  CHECK(get_ptr(s) == text_heap && text_live);
  memset(text_heap, 0xdd, TEXT_HEAP);
  text_live = 0;
  text_frees++;
}
// input_by_name(manager, name) -> the input, or null
static void *game_by_name(void *manager, const char *name) {
  by_name_calls++;
  CHECK(manager == manager_obj && name);
  snprintf(by_name_seen, sizeof by_name_seen, "%s", name);
  return input_name && !strcmp(name, input_name) ? input_obj : 0;
}

// The game's request for a menu movie (the white loading screen's resend): what it was given, and its answer
static void game_request(const void *movie, uint32_t lo, uint32_t hi);

// A file system object's question, has it this file: the whole file system's, and three archives', which answer
// for a path from where they are mounted. Each counts what it was asked.
#define ARCHIVES 3
static uint8_t *fs_whole, *fs_arch[ARCHIVES];
static int whole_asks, arch_asks[ARCHIVES];
static const char *whole_has, *arch_has[ARCHIVES];  // the one file each has beside an archive's appid.appid; null: none
static int arch_appid[ARCHIVES];                    // the archive answers for its appid.appid
static char whole_asked[200], arch_asked[200];      // the last path each kind was asked
static uint32_t game_fs_exists(uint8_t *fs, const char *path) {
  CHECK(fs && path);  // null: the runtime could not hand the game what it was given
  if (fs == fs_whole) {
    whole_asks++;
    snprintf(whole_asked, sizeof whole_asked, "%s", path);
    return 0xffffff00u | (uint32_t)(whole_has && !strcmp(path, whole_has));  // al, the rest whatever it was
  }
  for (int k = 0; k < ARCHIVES; k++) {
    if (fs != fs_arch[k]) continue;
    arch_asks[k]++;
    if (!strcmp(path, "appid.appid")) return (uint32_t)arch_appid[k];
    snprintf(arch_asked, sizeof arch_asked, "%s", path);
    return arch_has[k] && !strcmp(path, arch_has[k]);
  }
  CHECK(!"a file system object the game has not");
  return 0;
}

// The scrolling Learn a Song page's game (alt_scroll below): what its functions do, whatever the convention
static void *g_find(uint8_t *node, const char *name);
static int g_remove(uint8_t *node, uint8_t *key);
static void g_merge(uint8_t *target, uint8_t *source, uint32_t replace);
static void g_set(uint8_t *node, uint8_t *key, uint8_t *value);
static void g_key(const char *text, uint8_t *out);
static void g_build(uint8_t *page);
static void g_cbuild(uint8_t *page);
static void g_focus(uint8_t *page, int steps, int down);
static void g_dtor(uint8_t *record);
static void g_ref(uint8_t *node, int add);

// --- what the runtimes need of an image: the game's functions above, by each one's convention
#ifdef RC_GUEST32
static uint32_t rd(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static void wr(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
// a guest `ret N` with eax; ecx and edx are the callee's to spoil
static void ret32(rc_ctx *c, uint32_t eax, uint32_t pops) {
  uint32_t sp = (uint32_t)c->r[4];
  c->r[0] = eax;
  c->r[1] = c->r[2] = 0xdeadbeefu;
  c->ret_to = rd(sp);
  c->r[4] = sp + 4 + pops;
}
static uint32_t arg32(rc_ctx *c, int k) { return rd((uint32_t)c->r[4] + 4 + 4 * (uint32_t)k); }
static void f_mode(rc_ctx *c) { ret32(c, game_mode(arg32(c, 0)), 0); }
static void f_profile(rc_ctx *c) {
  game_profile_string(arg32(c, 0), as_host(arg32(c, 1)), as_host(arg32(c, 2)));
  ret32(c, 1, 0);
}
static void f_manager(rc_ctx *c) {
  manager_calls++;
  ret32(c, (uint32_t)as_game(manager_obj), 0);
}
static void f_by_name(rc_ctx *c) {  // stdcall
  ret32(c, (uint32_t)as_game(game_by_name(as_host(arg32(c, 0)), as_host(arg32(c, 1)))), 8);
}
static void f_destroy(rc_ctx *c) {  // thiscall: the string in ecx
  game_str_destroy(as_host((uint32_t)c->r[1]));
  ret32(c, 0, 0);
}
static void f_fs_exists(rc_ctx *c) {  // thiscall: the object in ecx, the path on the stack, `ret 4`
  ret32(c, game_fs_exists(as_host((uint32_t)c->r[1]), as_host(arg32(c, 0))), 4);
}
static void f_request(rc_ctx *c) {  // custom: the movie in eax, the id's words on the stack, `ret 8`
  uint32_t movie = (uint32_t)c->r[0];
  game_request(as_host(movie), arg32(c, 0), arg32(c, 1));
  ret32(c, 0, 8);
}
// the scroll game's functions (g_* below): thiscall ones have their object in ecx, stdcall ones everything on the stack
static void f_find(rc_ctx *c) {  // thiscall (node), the name in eax
  ret32(c, (uint32_t)as_game(g_find(as_host((uint32_t)c->r[1]), as_host((uint32_t)c->r[0]))), 0);
}
static void f_remove(rc_ctx *c) { ret32(c, g_remove(as_host(arg32(c, 0)), as_host(arg32(c, 1))), 8); }
static void f_merge(rc_ctx *c) {  // thiscall (target), source, replace
  g_merge(as_host((uint32_t)c->r[1]), as_host(arg32(c, 0)), arg32(c, 1));
  ret32(c, 0, 8);
}
static void f_set(rc_ctx *c) {
  g_set(as_host(arg32(c, 0)), as_host(arg32(c, 1)), as_host(arg32(c, 2)));
  ret32(c, 0, 12);
}
static void f_key(rc_ctx *c) {  // thiscall (text), &out
  g_key(as_host((uint32_t)c->r[1]), as_host(arg32(c, 0)));
  ret32(c, 0, 4);
}
static void f_build(rc_ctx *c) { g_build(as_host((uint32_t)c->r[1])); ret32(c, 0, 0); }
static void f_fup(rc_ctx *c) { g_focus(as_host((uint32_t)c->r[1]), (int)arg32(c, 0), 0); ret32(c, 0, 4); }
static void f_fdown(rc_ctx *c) { g_focus(as_host((uint32_t)c->r[1]), (int)arg32(c, 0), 1); ret32(c, 0, 4); }
static void f_cbuild(rc_ctx *c) { g_cbuild(as_host((uint32_t)c->r[1])); ret32(c, 0, 0); }
static void f_dtor(rc_ctx *c) { g_dtor(as_host((uint32_t)c->r[1])); ret32(c, 0, 0); }
static void f_addref(rc_ctx *c) { g_ref(as_host((uint32_t)c->r[1]), 1); ret32(c, 0, 0); }
static void f_release(rc_ctx *c) { g_ref(as_host((uint32_t)c->r[1]), 0); ret32(c, 0, 0); }
rc_fn rc32_lookup(uint32_t g) {
  if (g == ADDR(G_FIND)) return f_find;
  if (g == ADDR(G_REMOVE)) return f_remove;
  if (g == ADDR(G_MERGE)) return f_merge;
  if (g == ADDR(G_SET)) return f_set;
  if (g == ADDR(G_KEY)) return f_key;
  if (g == ADDR(G_BUILD)) return f_build;
  if (g == ADDR(G_CBUILD)) return f_cbuild;
  if (g == ADDR(G_FUP)) return f_fup;
  if (g == ADDR(G_FDOWN)) return f_fdown;
  if (g == ADDR(G_DTOR)) return f_dtor;
  if (g == ADDR(G_ADDREF)) return f_addref;
  if (g == ADDR(G_RELEASE)) return f_release;
  return g == ADDR(MODE) ? f_mode : g == ADDR(PROFILE) ? f_profile : g == ADDR(MANAGER) ? f_manager
       : g == ADDR(BYNAME) ? f_by_name : g == ADDR(DESTROY) ? f_destroy : g == ADDR(FSEXISTS) ? f_fs_exists
       : g == ADDR(REQUEST) ? f_request : 0;
}
int rc32_tls_images(const rc32_tls_image **out) {
  *out = 0;
  return 0;
}
#else
uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0) {
  (void)dbls; (void)ndbl; (void)xmm0;
  switch (fn - rc_slide) {
    case ADDR(MODE):
      CHECK_EQ(nint, 1);
      return game_mode((uint32_t)ints[0]);
    case ADDR(PROFILE):
      CHECK_EQ(nint, 3);
      game_profile_string((uint32_t)ints[0], as_host(ints[1]), as_host(ints[2]));
      return 1;
    case ADDR(MANAGER):
      manager_calls++;
      return as_game(manager_obj);
    case ADDR(BYNAME):
      CHECK_EQ(nint, 2);
      return as_game(game_by_name(as_host(ints[0]), as_host(ints[1])));
    case ADDR(DESTROY):
      CHECK_EQ(nint, 1);
      game_str_destroy(as_host(ints[0]));
      return 0;
    case ADDR(REQUEST): {
      CHECK_EQ(nint, 2);
      const uint32_t *id = as_host(ints[1]);
      game_request(as_host(ints[0]), id[0], id[1]);
      return 0;
    }
    case ADDR(G_FIND): return as_game(g_find(as_host(ints[0]), as_host(ints[1])));
    case ADDR(G_REMOVE): return (uint64_t)g_remove(as_host(ints[0]), as_host(ints[1]));
    case ADDR(G_MERGE): g_merge(as_host(ints[0]), as_host(ints[1]), (uint32_t)ints[2]); return 0;
    case ADDR(G_SET): g_set(as_host(ints[0]), as_host(ints[1]), as_host(ints[2])); return 0;
    case ADDR(G_KEY): g_key(as_host(ints[1]), as_host(ints[0])); return 0;
    case ADDR(G_BUILD): g_build(as_host(ints[0])); return 0;
    case ADDR(G_CBUILD): g_cbuild(as_host(ints[0])); return 0;
    case ADDR(G_FUP): g_focus(as_host(ints[0]), (int)ints[1], 0); return 0;
    case ADDR(G_FDOWN): g_focus(as_host(ints[0]), (int)ints[1], 1); return 0;
    case ADDR(G_DTOR): g_dtor(as_host(ints[0])); return 0;
  }
  return 0;
}
// OV_VCALL, the Mac app's (overrides/src/rc_ov.c): the one virtual call the fixes here make
uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs) {
  if (slot == (int)rc_off("json_node_add_ref_slot") || slot == (int)rc_off("json_node_release_slot")) {
    g_ref(as_host(obj), slot == (int)rc_off("json_node_add_ref_slot"));
    return 0;
  }
  CHECK_EQ((uint64_t)slot, (uint64_t)rc_off("fs_exists_slot"));
  CHECK_EQ(nargs, 2);
  CHECK_EQ(args[0], obj);
  return game_fs_exists(as_host(obj), as_host(args[1]));
}
#endif

static const char *const SWITCHES[] = {"RS_MODS_LOG", "RS_MODS_BYPASS", "RS_MODS_FIXES", "RS_MODS_INTRO",
                                       "RS_MODS_OVERLAY", "RS_MODS_AUTOSAVE", "RS_MODS_MULTIPLAYER",
                                       "RS_MODS_BUDGET_MS", "RS_MODS_FRAME_RATE", "RS_MODS_GUITARCADE_SKIP",
                                       "RS_MODS_MENU_RETRY", "RS_MODS_ARRANGEMENTS"};
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
  CHECK(c->guitarcade_skip && c->menu_retry && c->arrangements);
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
  setenv("RS_MODS_GUITARCADE_SKIP", "0", 1);
  setenv("RS_MODS_MENU_RETRY", "0", 1);
  setenv("RS_MODS_ARRANGEMENTS", "no", 1);
  rs_mods_reload_config();
  CHECK(!c->intro && !c->bypass && c->log && c->fixes && c->overlay && c->autosave && c->multiplayer);
  CHECK(!c->guitarcade_skip && !c->menu_retry && !c->arrangements);
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
// frame rate fixes find none of their points in this test's tables; Skip Tuner, the menu movie request and the
// arrangements find theirs.
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
  CHECK_EQ(logged_lines("[rs-mods] Guitarcade Skip Tuner: on"), 1);
  CHECK_EQ(logged_lines("[rs-mods] menu movie request again: on"), 1);
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song alternate arrangements: on, scrolling"), 1);
  CHECK_EQ(logged_lines("[rs-mods] co-op arrangement list: on"), 1);
  CHECK_EQ(logged_lines("[rs-mods] no intro hook point") + logged_lines("[rs-mods] no DLC ownership hook point") +
           logged_lines("[rs-mods] no hook point for Player 2's saved input"), 0);
  CHECK(!strstr(logged, "no points for"));  // the batched mounting's four mid points
  CHECK(!rs_mods_cfg.autosave && rs_mods_cfg.log);
}

// --- a call of a hook point as the game makes it, and the game's function behind it
static const uint32_t RET = 0x12345678;
static rc_ctx *ctx;
static uint8_t *obj;       // the game's object, as the host sees it
static uint8_t *vtable_of_movies;  // the first word of the menu movies of the tests below
static int orig_ran;
static uint32_t orig_arg;  // what the original was given, where its convention has it (the low word)
#ifdef RC_GUEST32
static uint32_t esp0, obj_guest;
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
// custom (input): the input in eax and nothing else, `ret`; al = it looked for a slot. esi is not the call's.
#define ESI 0x5a5a5a5au
static void o_bind(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  orig_arg = (uint32_t)c->r[0];
  c->r[0] = 0xffffff01u;
  c->ret_to = rd(sp);
  c->r[4] = sp + 4;
}
static void call_bind(uint32_t input) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  ctx->r[0] = input;
  ctx->r[6] = ESI;
  wr(sp, RET);
  orig_ran = 0;
  rc_hook_call(BIND, ctx, o_bind);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], sp + 4);
  CHECK_EQ(ctx->r[6], ESI);
}
// cdecl, no argument: a frame; al = the game does not quit
static void o_frame(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  c->r[0] = 0xffffff00u;
  c->ret_to = rd(sp);
  c->r[4] = sp + 4;
}
static void call_frame(void) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  wr(sp, RET);
  orig_ran = 0;
  rc_hook_call(FRAME, ctx, o_frame);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ(orig_ran, 1);
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
// (input, force): force 0, as in every call of the game's
static void o_bind(rc_ctx *c) {
  orig_ran++;
  orig_arg = (uint32_t)c->r[7];
  c->r[0] = 1;
  c->r[4] += 8;
}
static void call_bind(uint32_t input) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  ctx->r[7] = input;
  ctx->r[6] = 0;
  orig_ran = 0;
  rc_hook_call(BIND, ctx, o_bind);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
}
static void o_frame(rc_ctx *c) {
  orig_ran++;
  c->r[0] = 0;
  c->r[4] += 8;
}
static void call_frame(void) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  orig_ran = 0;
  rc_hook_call(FRAME, ctx, o_frame);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
  CHECK_EQ(orig_ran, 1);
}
rc_ctx *rc_thread_ctx(void) { return ctx; }  // the runtime's (rc_thread.c): this test's one context
#define OBJ_AS_GIVEN ((uint32_t)(uintptr_t)obj)
#endif

// Skip Tuner in a Guitarcade game's tuner: at the point the game has its answer, 0 for a tuner that goes on to a
// Guitarcade game. The hook makes it 1 and lets the game's own test run; switched off, the answer is the game's.
static void skip_tuner(void) {
  uint32_t fl = 0x40;
#ifdef RC_GUEST32
  ctx->r[5] = esp0 - 64;  // the frame pointer: the answer is a byte below it, with the game's other locals around
  uint8_t *local = (uint8_t *)RC32_H((uint32_t)ctx->r[5] + (uint32_t)SKIP_LOCAL);
  local[-1] = 0x5a;
  local[1] = 0xa5;
#define ANSWER (*local)
#define SET_ANSWER(v) (*local = (v))
#define AROUND_KEPT (local[-1] == 0x5a && local[1] == 0xa5)
#else
#define ANSWER ((uint8_t)ctx->r[14])
#define SET_ANSWER(v) (ctx->r[14] = 0x1122334455667700ull | (uint8_t)(v))
#define AROUND_KEPT ((ctx->r[14] >> 8) == 0x11223344556677ull)
#endif
  no_switches();
  rs_mods_reload_config();
  SET_ANSWER(0);
  CHECK_EQ((uint32_t)rc_mid_call(0, ctx, &fl), 0);  // no continuation: the game's test, on the new answer
  CHECK_EQ(ANSWER, 1);
  CHECK(AROUND_KEPT);
  CHECK_EQ(fl, 0x40);
  SET_ANSWER(1);  // a song's tuner: offered already
  rc_mid_call(0, ctx, &fl);
  CHECK_EQ(ANSWER, 1);
  setenv("RS_MODS_GUITARCADE_SKIP", "0", 1);
  rs_mods_reload_config();
  SET_ANSWER(0);
  rc_mid_call(0, ctx, &fl);
  CHECK_EQ(ANSWER, 0);  // the original
  CHECK(AROUND_KEPT);
  no_switches();
  rs_mods_reload_config();
#undef ANSWER
#undef SET_ANSWER
#undef AROUND_KEPT
}

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

// Player 2's saved input (the hook start() added on input_bind). The call a hook sees there is the Mac's on both
// runtimes: the input, and a force of 0, which Learn & Play's function has folded away.
static uint32_t seen_input, seen_force;
static int seen;
static int see_bind(rc_ctx *c, void *user) {
  (void)user;
  seen++;
  seen_input = (uint32_t)c->r[7];
  seen_force = (uint32_t)c->r[6];
  return 0;
}

enum { P1_GUITAR, P1_MIC, P2_GUITAR, P2_MIC, NONE = 5, SLOTS = 10 };
static int32_t *binding;
// The game before a call: Player 2's mode, what their profile names, the input the manager has and its slot, and
// every slot free but slot 0, Player 1's guitar. The words on either side of the ten slots read as free ones too:
// a write past the table would show.
static void bind_at(int mode, const char *in_profile, const char *name, uint32_t slot) {
  game_mode_is = mode;
  profile_text = in_profile;
  input_name = name;
  memcpy(input_obj + rc_off("input_slot"), &slot, 4);
  for (int k = -1; k <= SLOTS; k++) binding[k] = k ? NONE : P1_GUITAR;
  mode_calls = profile_calls = manager_calls = by_name_calls = seen = text_frees = destroy_calls = 0;
  profile_key = 0;
  by_name_seen[0] = 0;
}
// A call for `input`: the game's own search runs after the fix whatever it did, with the input it was called with.
// A string the fix had the game fill is destroyed by then, and so a text the game allocated for it is freed.
static void bind(uint32_t input) {
  call_bind(input);
  CHECK_EQ(orig_ran, 1);
  CHECK_EQ(orig_arg, input);
  CHECK_EQ(seen, 1);
  CHECK_EQ(seen_input, input);
  CHECK_EQ(seen_force, 0);
  CHECK_EQ(destroy_calls, profile_calls);
  CHECK(!text_live);
}
static int slots_as_set(void) {
  for (int k = -1; k <= SLOTS; k++)
    if (binding[k] != (k ? NONE : P1_GUITAR)) return 0;
  return 1;
}

static void saved_input(void) {
  static const char SHORT[] = "USB Codec", LONG[] = "Interface with a long name: Input 2";  // in place, and not
  CHECK(rs_mods_cfg.multiplayer);
  CHECK(sizeof SHORT - 1 < (size_t)rc_off("efd_str_size") && sizeof LONG - 1 > (size_t)rc_off("efd_str_size"));
  CHECK(!rc_hook_add_name("input_bind", see_bind, NULL, NULL));
  // another input than Player 2's guitar: nothing asked of the game
  static const uint32_t others[] = {P1_GUITAR, P1_MIC, P2_MIC, NONE};
  for (size_t k = 0; k < sizeof others / sizeof *others; k++) {
    bind_at(3, SHORT, SHORT, 4);
    bind(others[k]);
    CHECK_EQ(mode_calls + profile_calls + by_name_calls, 0);
    CHECK(slots_as_set());
  }
  // Player 2 on a cable, disconnected or not set: the game's own rule
  static const int cable[] = {0, 2, 4};
  for (size_t k = 0; k < sizeof cable / sizeof *cable; k++) {
    bind_at(cable[k], SHORT, SHORT, 4);
    bind(P2_GUITAR);
    CHECK_EQ(mode_calls, 1);
    CHECK_EQ(mode_player, 1);
    CHECK_EQ(profile_calls + by_name_calls, 0);
    CHECK(slots_as_set());
  }
  // Direct Connect, the name in place: the profile's Direct Connect key, the input of that name, its slot
  bind_at(3, SHORT, SHORT, 4);
  log_start();
  bind(P2_GUITAR);
  log_stop();
  CHECK_EQ(profile_calls, 1);
  CHECK_EQ(profile_player, 1);
  CHECK(profile_key == (const char *)data + D_TEXT_DIRECT);
  CHECK_EQ(manager_calls, 1);
  CHECK_EQ(by_name_calls, 1);
  CHECK(!strcmp(by_name_seen, SHORT));
  CHECK_EQ(text_frees, 0);  // in place: nothing of the game's to free
  CHECK_EQ(binding[4], P2_GUITAR);
  binding[4] = NONE;
  CHECK(slots_as_set());
  CHECK_EQ(logged_lines("[rs-mods] player 2 on \"USB Codec\""), 1);
  // the microphone, the name behind a pointer: the other key, and a slot a microphone input holds is taken
  static const int32_t taken[] = {P1_MIC, P2_MIC};
  for (size_t k = 0; k < sizeof taken / sizeof *taken; k++) {
    bind_at(1, LONG, LONG, 9);
    binding[9] = taken[k];
    log_start();
    bind(P2_GUITAR);
    log_stop();
    CHECK_EQ(logged_lines("[rs-mods] player 2 on \"Interface with a long name: Input 2\""), 1);
    CHECK(profile_key == (const char *)data + D_TEXT_MIC);
    CHECK(!strcmp(by_name_seen, LONG));  // the name as it was before its text was freed
    CHECK_EQ(text_frees, 1);
    CHECK_EQ(binding[9], P2_GUITAR);
  }
  // a slot a guitar holds stays that guitar's, and nothing is said
  bind_at(3, SHORT, SHORT, 0);
  log_start();
  bind(P2_GUITAR);
  log_stop();
  CHECK_EQ(by_name_calls, 1);
  CHECK(slots_as_set());
  CHECK(!strstr(logged, "player 2 on"));
  // Player 2's guitar has a slot already, any of the ten: nothing read of the profile
  static const int has[] = {1, 7, SLOTS - 1};
  for (size_t k = 0; k < sizeof has / sizeof *has; k++) {
    bind_at(3, SHORT, SHORT, 4);
    binding[has[k]] = P2_GUITAR;
    bind(P2_GUITAR);
    CHECK_EQ(mode_calls, 1);
    CHECK_EQ(profile_calls + by_name_calls, 0);
    CHECK_EQ(binding[4], NONE);
  }
  // no saved input in the profile, as an empty text and as a string with no text; one the manager doesn't have
  static const char *const none[] = {"", 0};
  for (int k = 0; k < 2; k++) {
    bind_at(3, none[k], SHORT, 4);
    bind(P2_GUITAR);
    CHECK_EQ(profile_calls, 1);
    CHECK_EQ(by_name_calls, 0);
    CHECK(slots_as_set());
  }
  bind_at(3, LONG, SHORT, 4);  // the interface of a long name unplugged: its text is freed all the same
  bind(P2_GUITAR);
  CHECK_EQ(by_name_calls, 1);
  CHECK_EQ(text_frees, 1);
  CHECK(slots_as_set());
  // a slot out of the table, by one, by a byte's worth and by all of its word: no slot is written, in it or past it
  static const uint32_t out_of_table[] = {SLOTS, 0x104, 0xffffffffu};
  for (size_t k = 0; k < sizeof out_of_table / sizeof *out_of_table; k++) {
    bind_at(3, SHORT, SHORT, out_of_table[k]);
    bind(P2_GUITAR);
    CHECK_EQ(by_name_calls, 1);
    CHECK(slots_as_set());
  }
  // a name longer than the fix keeps: the input is asked for by its first 255 bytes, and nothing past them is written
  static char longest[300];
  memset(longest, 'n', sizeof longest - 1);
  bind_at(3, longest, SHORT, 4);
  bind(P2_GUITAR);
  CHECK_EQ(by_name_calls, 1);
  CHECK_EQ(strlen(by_name_seen), 255);
  CHECK(!memcmp(by_name_seen, longest, 255));
  CHECK(slots_as_set());
  // the image without the key's pointer: nothing asked of the profile
  uint8_t kept[8];
  memcpy(kept, data + D_KEY_DIRECT, sizeof kept);
  memset(data + D_KEY_DIRECT, 0, sizeof kept);
  bind_at(3, SHORT, SHORT, 4);
  bind(P2_GUITAR);
  CHECK_EQ(mode_calls, 1);
  CHECK_EQ(profile_calls + by_name_calls, 0);
  CHECK(slots_as_set());
  memcpy(data + D_KEY_DIRECT, kept, sizeof kept);
  // RS_MODS_MULTIPLAYER=0: the original
  rs_mods_cfg.multiplayer = 0;
  bind_at(3, SHORT, SHORT, 4);
  bind(P2_GUITAR);
  CHECK_EQ(mode_calls + profile_calls + by_name_calls, 0);
  CHECK(slots_as_set());
  rs_mods_cfg.multiplayer = 1;
}

// --- Batched DLC mounting: what both editions' loops share. The calls below are an edition's dlc_update's
// (mods_shared.h), and the game's side is its mid points, called as the translated code calls them.
#define STARTS(text, with) (!strncmp((text), with, sizeof with - 1))
// The end of a mounting whose lines are not looked at
static void ends_quietly(void) {
  log_start();
  rs_mods_mount_end();
  log_stop();
}
// One update of the coordinator with its flag set: the first starts a mounting
static int frame(void) { return rs_mods_mount_frame(1); }
#ifdef RC_GUEST32
#define STACK_FOR_CALLS() (ctx->r[4] = esp0 - 1024, ctx->r[5] = esp0 - 64)
#else
#define STACK_FOR_CALLS() ((void)0)
#endif

// A response the ManifestManager's handler is given: the asset's name in the game's string, in place or behind a
// pointer. A mount's manifest was asked for by path: its name is empty.
static uint8_t *response_named(const char *name, int in_place) {
  uint8_t *r = game_alloc(128), *str = r + rc_off("asset_load_name");
  uint8_t *end = str + rc_off("efd_str_size"), *mark = str + rc_off("efd_str_inline");
  size_t n = strlen(name);
  if (in_place) {
    CHECK(n < (size_t)rc_off("efd_str_size"));
    memcpy(str, name, n + 1);
    put_ptr(end, str + n);
    put_ptr(mark, end);
  } else {
    uint8_t *text = game_alloc((uint32_t)n + 1);
    memcpy(text, name, n + 1);
    put_ptr(str, text);
    put_ptr(end, text + n);
    put_ptr(mark, text + n + 1);
  }
  return r;
}
static void manifest_asked(void) {
  uint32_t fl = 0;
  STACK_FOR_CALLS();
  CHECK_EQ((uint32_t)rc_mid_call(M_REQUEST, ctx, &fl), 0);
}
static void manifest_answered(uint8_t *response) {
  uint32_t fl = 0;
  STACK_FOR_CALLS();
#ifdef RC_GUEST32
  ctx->r[1] = as_game(manager_obj);
  wr((uint32_t)ctx->r[5] + 8, (uint32_t)as_game(response));
#else
  ctx->r[7] = as_game(manager_obj);
  ctx->r[6] = as_game(response);
#endif
  CHECK_EQ((uint32_t)rc_mid_call(M_RESPONSE, ctx, &fl), 0);
}

// The song manifests: the flag is held from the step that found the list empty until every manifest this mounting
// asked for has been answered. Answers with a name are the game's other loads, and one asked for before the
// mounting is not this mounting's. The loader thread's pause is short for as long as archives are mounted.
static void manifests(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  uint8_t *unnamed = response_named("", 1), *unnamed_far = response_named("", 0);
  uint8_t *named = response_named("urn:x:guitar", 1);
  uint8_t *named_far = response_named("urn:database:json-db:a-long-name", 0);
  CHECK_EQ(rs_mods_loader_pause_us(), 10000);  // the game's own
  manifest_asked();  // before the mounting: not waited for
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_loader_pause_us(), 200);
  CHECK(!rs_mods_mount_holding());
  for (int k = 0; k < 3; k++) {
    rs_mods_mounting(1);
    manifest_asked();
    rs_mods_mounting(0);
    rs_mods_mount_took();
  }
  manifest_answered(unnamed);
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_mount_listed(), 1);  // two to come: the update sets the flag again
  CHECK(rs_mods_mount_holding());
  CHECK_EQ(frame(), 1);
  CHECK(rs_mods_mount_waiting());
  manifest_answered(named);
  manifest_answered(named_far);
  manifest_answered(0);  // the handler given nothing
  CHECK(rs_mods_mount_waiting());
  manifest_answered(unnamed_far);
  CHECK(rs_mods_mount_waiting());
  manifest_answered(unnamed);
  CHECK(!rs_mods_mount_waiting());
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 3 archives mounted in 2 frames, "));
  CHECK(strstr(logged, " s, their songs in the lists ") && strstr(logged, " s later\n"));
  CHECK(!strstr(logged, "file questions"));  // none asked
  CHECK(!rs_mods_mount_holding());
  CHECK_EQ(rs_mods_mount_frame(0), 0);  // the flag clear: no mounting, and the game's pause
  CHECK_EQ(rs_mods_loader_pause_us(), 10000);
  // every manifest in when the list is empty: no hold, and the line without the manifests' part
  CHECK_EQ(frame(), 1);
  rs_mods_mounting(1);
  manifest_asked();
  rs_mods_mounting(0);
  rs_mods_mount_took();
  manifest_answered(unnamed);
  CHECK_EQ(rs_mods_mount_listed(), 0);
  CHECK(!rs_mods_mount_holding());
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 1 archives mounted in 1 frames, ") && !strstr(logged, "their songs"));
  // what an edition's loop waits for between two steps: while its number for what is left is not 0
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_mount_loading(0), 0);
  CHECK_EQ(rs_mods_mount_loading(3), 1);
  CHECK_EQ(rs_mods_mount_loading(5), 1);
  CHECK_EQ(rs_mods_mount_loading(0), 0);
  // a manifest that never comes: after 5 s without an answer the mounting waits for nothing more
  manifest_asked();
  CHECK_EQ(rs_mods_mount_listed(), 1);
  CHECK(rs_mods_mount_waiting());
  usleep(5100000);
  log_start();
  CHECK(!rs_mods_mount_waiting());
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] DLC: a song manifest was not answered in 5 s, the mounting goes on without "
                        "waiting"), 1);
  CHECK_EQ(rs_mods_mount_loading(7), 0);
  ends_quietly();
  manifest_answered(unnamed);  // it came after all: the next mounting starts from the counts as they are
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_mount_loading(7), 1);  // and waits again
  CHECK_EQ(rs_mods_mount_listed(), 0);
  ends_quietly();
  // RS_MODS_FIXES=0 and a budget of 0: a step a frame, the game's pause
  rs_mods_cfg.fixes = 0;
  CHECK_EQ(frame(), 0);
  CHECK_EQ(rs_mods_loader_pause_us(), 10000);
  ends_quietly();
  rs_mods_cfg.fixes = 1;
  rs_mods_cfg.budget_s = 0;
  CHECK_EQ(frame(), 0);
  ends_quietly();
  no_switches();
  rs_mods_reload_config();
}

// The game mounts the archive `fs` at `at` (fs_mount): the mount point and where the object's pointer is
static void mounts(const char *at, uint8_t *fs) {
  static char *where;
  static uint8_t *slot;
  uint32_t fl = 0;
  if (!where) where = game_alloc(64), slot = game_alloc(8);
  strcpy(where, at);
  put_ptr(slot, fs);
  STACK_FOR_CALLS();
#ifdef RC_GUEST32
  wr((uint32_t)ctx->r[5] + 8, (uint32_t)as_game(where));
  ctx->r[0] = as_game(slot);
#else
  ctx->r[7] = as_game(where);
  ctx->r[6] = as_game(slot);
#endif
  CHECK_EQ((uint32_t)rc_mid_call(M_MOUNT, ctx, &fl), 0);
}
// The game asks its whole file system for `path` (fs_exists): the answer it goes on with. Where the fix has
// answered, the game's own call is skipped; where it has not, the game makes it.
static int asks_answered;
static int asks(const char *path) {
  static char *text;
  uint32_t fl = 0;
  if (!text) text = game_alloc(256);
  strcpy(text, path);
  STACK_FOR_CALLS();
#ifdef RC_GUEST32
  ctx->r[1] = as_game(fs_whole);
  ctx->r[0] = as_game(text);
#else
  ctx->r[7] = as_game(fs_whole);
  ctx->r[6] = as_game(text);
  ctx->r[0] = 0x1122334455667700ull;
#endif
  int go = rc_mid_call(M_EXISTS, ctx, &fl);
  asks_answered = go != 0;
  if (!go) return (uint8_t)game_fs_exists(fs_whole, text) != 0;
  CHECK_EQ(go, 1);
  return (uint8_t)ctx->r[0] != 0;
}
// One install step that mounts `fs` at `at` (null: it mounts nothing) and asks for `n` of the paths
static const char *const QUESTION[] = {"data/shaders/generated/a.sha1", "data/shaders/generated/b.sha1",
                                       "data/shaders/generated/c.sha1"};
static int step_found[3];
static void step(const char *at, uint8_t *fs, int n) {
  rs_mods_mounting(1);
  if (at) mounts(at, fs);
  for (int k = 0; k < n; k++) step_found[k] = asks(QUESTION[k]);
  rs_mods_mounting(0);
  rs_mods_mount_took();
}
static void fs_reset(void) {
  whole_asks = 0;
  whole_has = 0;
  for (int k = 0; k < ARCHIVES; k++) arch_asks[k] = 0, arch_has[k] = 0, arch_appid[k] = 1;
}

// The cache overlay's names (rs_mods_overlay_name): a cacheN.7z as it is, a menu movie of static.psarc in lower case
// with '/', nothing for any other file, a path in the overlay already, or a name that doesn't fit.
static void overlay_names(void) {
  char alt[64];
  CHECK(rs_mods_overlay_name("cache8.7z", alt, sizeof alt) && !strcmp(alt, "patchupdate/cache8.7z"));
  CHECK(rs_mods_overlay_name("Cache12.7z", alt, sizeof alt) && !strcmp(alt, "patchupdate/Cache12.7z"));
  CHECK(rs_mods_overlay_name("data\\gfxassets\\views\\TD_SongList.gfx", alt, sizeof alt) &&
        !strcmp(alt, "patchupdate/gfxassets/views/td_songlist.gfx"));
  CHECK(rs_mods_overlay_name("gfxassets/views/td_songlist.gfx", alt, sizeof alt) &&
        !strcmp(alt, "patchupdate/gfxassets/views/td_songlist.gfx"));
  CHECK(rs_mods_overlay_name("/game/data/gfxassets/views/x.gfx", alt, sizeof alt) &&
        !strcmp(alt, "patchupdate/gfxassets/views/x.gfx"));
  const char *not[] = {"cache.7z", "cache8.7z.bak", "data\\gfxassets\\libs\\commonlib.gfx",
                       "data\\gfxassets\\tuner.gfx", "data\\gfxassets\\views\\sub\\x.gfx",
                       "data\\gfxassets\\views\\x.dds", "data\\gfxassets\\views\\.gfx",
                       "data\\mygfxassets\\views\\x.gfx", "patchupdate/gfxassets/views/x.gfx", "PatchUpdate/cache8.7z",
                       "dlc/x_m.psarc", ""};
  for (size_t i = 0; i < sizeof not / sizeof *not; i++) CHECK(!rs_mods_overlay_name(not[i], alt, sizeof alt));
  CHECK(!rs_mods_overlay_name(NULL, alt, sizeof alt));
  CHECK(!rs_mods_overlay_name("data\\gfxassets\\views\\a_name_too_long_for_the_buffer.gfx", alt, 40));
  CHECK(!rs_mods_overlay_name("cache8.7z", alt, 21) && rs_mods_overlay_name("cache8.7z", alt, 22));
}

// A step's file questions: one the step before had "no" for is asked of the archive this step mounted alone, by
// the path from its mount point, and the whole file system is asked whenever that is not known to be enough.
static void file_questions(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);  // the first step: nothing known
  CHECK_EQ(arch_asks[0], 1);    // its appid.appid
  CHECK_EQ(whole_asks, 3);
  CHECK(asks_answered && !step_found[0] && !step_found[1] && !step_found[2]);
  step("Data", fs_arch[1], 3);  // the second: its own archive alone, whatever the mount point's case
  CHECK_EQ(whole_asks, 3);
  CHECK_EQ(arch_asks[1], 4);
  CHECK(!strcmp(arch_asked, "shaders/generated/c.sha1"));
  CHECK(asks_answered && !step_found[0] && !step_found[1] && !step_found[2]);
  // an archive that brings one of the files: its "yes" goes to the whole file system, whose answer is the game's
  arch_has[2] = "shaders/generated/b.sha1";
  whole_has = QUESTION[1];
  step("data", fs_arch[2], 3);
  CHECK_EQ(whole_asks, 4);
  CHECK(!strcmp(whole_asked, QUESTION[1]));
  CHECK(!step_found[0] && step_found[1] && !step_found[2]);
  step("data", fs_arch[0], 3);  // and a "yes" is not kept: the whole file system again, and the others alone
  CHECK_EQ(whole_asks, 5);
  CHECK(!step_found[0] && step_found[1] && !step_found[2]);
  CHECK_EQ(arch_asks[0], 1 + 1 + 2);
  whole_has = 0;  // (gone again)
  step("data", fs_arch[1], 3);
  CHECK_EQ(whole_asks, 6);  // b: last "yes"
  // something mounted outside a step: nothing is known in the next one
  mounts("data", fs_arch[2]);
  step("data", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 9);
  step("data", fs_arch[1], 3);
  CHECK_EQ(whole_asks, 9);
  // a mount point a path can't be taken from, an archive that does not answer for its appid.appid, a step that
  // mounts nothing: the whole file system
  step("data/songs", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 12);
  arch_appid[1] = 0;
  step("data", fs_arch[1], 3);
  CHECK_EQ(whole_asks, 15);
  arch_appid[1] = 1;
  step(0, 0, 3);
  CHECK_EQ(whole_asks, 18);
  // a file that is not under the step's mount point can't have come with it
  int at_other = arch_asks[0];
  step("other", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 18);
  CHECK_EQ(arch_asks[0], at_other + 1);
  CHECK(asks_answered && !step_found[0]);
  // the mounting's end: each question once more to the whole file system, and the count in the log
  CHECK_EQ(rs_mods_mount_listed(), 0);
  CHECK_EQ(whole_asks, 21);
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 11 archives mounted in 1 frames, "));
  CHECK_EQ(logged_lines("[rs-mods] DLC: 15 of the steps' 33 file questions asked of the step's archive alone"), 1);
  // the next mounting knows nothing of the last one's answers
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 3);
  rs_mods_mount_listed();
  ends_quietly();
  // RS_MODS_FIXES=0: the questions are the game's own
  rs_mods_cfg.fixes = 0;
  fs_reset();
  CHECK_EQ(frame(), 0);
  step("data", fs_arch[0], 3);
  CHECK(!asks_answered);
  step("data", fs_arch[1], 3);
  CHECK(!asks_answered);
  CHECK_EQ(whole_asks, 6);
  CHECK_EQ(arch_asks[0] + arch_asks[1], 0);
  ends_quietly();
  rs_mods_cfg.fixes = 1;
  // An answer that was not the file system's (the file is there, by a way this does not know): the check at the
  // mounting's end finds it, says so, and the questions are the game's own for the rest of the run.
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  whole_has = QUESTION[2];
  step("data", fs_arch[1], 3);
  CHECK(asks_answered && !step_found[2]);
  log_start();
  CHECK_EQ(rs_mods_mount_listed(), 0);
  rs_mods_mount_end();
  log_stop();
  CHECK(strstr(logged, "[rs-mods] DLC: data/shaders/generated/c.sha1 is there, and a step was told it was not: "));
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  CHECK(!asks_answered);
  step("data", fs_arch[1], 3);
  CHECK(!asks_answered && step_found[2]);
  ends_quietly();
  no_switches();
  rs_mods_reload_config();
}

// A mounting that does not go by the book: an enumeration the game starts while the flag is held, a flag cleared
// outside the update, steps one a frame, a mounting whose steps ask nothing, and a question asked before the
// step's mount.
static void mounting_edges(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  uint8_t *unnamed = response_named("", 1);
  // The game starts an enumeration while the flag is held for manifests: the hold ends, the new archives are
  // stepped as a mounting of their own, and the manifests still to come are waited for with it.
  fs_reset();
  CHECK_EQ(frame(), 1);
  rs_mods_mounting(1);
  manifest_asked();
  manifest_asked();
  rs_mods_mounting(0);
  rs_mods_mount_took();
  CHECK_EQ(rs_mods_mount_listed(), 1);
  CHECK(rs_mods_mount_holding());
  log_start();
  rs_mods_mount_restarted();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: a new enumeration with 2 song manifests of the last one still to come\n"));
  CHECK(strstr(logged, "[rs-mods] DLC: 1 archives mounted in 1 frames, "));
  CHECK(!rs_mods_mount_holding());
  CHECK_EQ(frame(), 1);  // the next update: steps again
  step("data", fs_arch[0], 3);
  CHECK(asks_answered);
  CHECK_EQ(whole_asks, 3);  // a first step: nothing is known from the mounting before
  manifest_answered(unnamed);
  CHECK_EQ(rs_mods_mount_listed(), 1);  // one of the two is still to come
  manifest_answered(unnamed);
  CHECK(!rs_mods_mount_waiting());
  ends_quietly();
  log_start();
  rs_mods_mount_restarted();  // with no hold: nothing
  log_stop();
  CHECK(!*logged);
  // The flag cleared by something else than the update that found it set: the next mounting starts clean.
  CHECK_EQ(frame(), 1);
  rs_mods_mounting(1);
  manifest_asked();
  rs_mods_mounting(0);
  rs_mods_mount_took();
  CHECK_EQ(rs_mods_mount_listed(), 1);
  log_start();
  CHECK_EQ(rs_mods_mount_frame(0), 0);
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: the mounting ended outside its update, after 1 archives in 1 frames\n"));
  CHECK(!rs_mods_mount_holding());
  manifest_answered(unnamed);
  CHECK_EQ(frame(), 1);
  CHECK(!rs_mods_mount_holding());
  CHECK_EQ(rs_mods_mount_listed(), 0);
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 0 archives mounted in 1 frames, "));
  // A step a frame (a budget of 0): the questions are the game's own, as with the fixes off; so is a step with
  // nothing being installed.
  fs_reset();
  rs_mods_cfg.budget_s = 0;
  CHECK_EQ(frame(), 0);
  step("data", fs_arch[0], 3);
  step("data", fs_arch[1], 3);
  CHECK(!asks_answered);
  CHECK_EQ(arch_asks[0] + arch_asks[1], 0);
  ends_quietly();
  rs_mods_reload_config();
  CHECK_EQ(rs_mods_mount_frame(0), 0);
  step("data", fs_arch[0], 3);
  CHECK(!asks_answered);
  // A mounting whose steps asked nothing checks nothing at its end: the answers kept are an earlier mounting's.
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  step("data", fs_arch[1], 3);
  CHECK_EQ(rs_mods_mount_listed(), 0);
  ends_quietly();
  int asked = whole_asks;
  whole_has = QUESTION[0];  // (a file that came since)
  CHECK_EQ(frame(), 1);
  step(0, 0, 0);
  log_start();
  CHECK_EQ(rs_mods_mount_listed(), 0);
  rs_mods_mount_end();
  log_stop();
  CHECK_EQ(whole_asks, asked);
  CHECK(!strstr(logged, "is there, and a step was told"));
  // A question a step asked before its mount is not known for the step after: the archive it then mounted may
  // have the file.
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 1);
  rs_mods_mounting(1);
  CHECK(!asks(QUESTION[0]));
  arch_has[1] = "shaders/generated/a.sha1";
  whole_has = QUESTION[0];
  mounts("data", fs_arch[1]);
  rs_mods_mounting(0);
  rs_mods_mount_took();
  step("data", fs_arch[2], 1);
  CHECK(step_found[0]);  // the whole file system's answer, not the third archive's alone
  log_start();
  CHECK_EQ(rs_mods_mount_listed(), 0);
  rs_mods_mount_end();
  log_stop();
  CHECK(!strstr(logged, "is there, and a step was told"));
  no_switches();
  rs_mods_reload_config();
}

// --- the white loading screen: a menu movie's request that the locator refuses is asked again when its file is
// indexed. The game's request is the function behind game_request: it runs the mid points as the game's does (its
// start, the locator call and its answer), and the answer is the test's. The points are driven here as the game
// would reach them.
static int request_calls, locator_accepts;  // requests made through the game's function; its answer to the next
static const void *request_movie;
static uint32_t request_id[2];
static uint8_t *mid_frame;                  // a block standing for the frame of the points: ebp / rbp is 0x600 into it
#define MID_FRAME (mid_frame + 0x600)
static void set_frame(void) { ctx->r[5] = as_game(MID_FRAME); }
static void put_string(uint8_t *str, const char *text) {  // a game string behind a pointer at its start
  size_t n = strlen(text);
  uint8_t *heap = game_alloc((uint32_t)n + 1);
  memcpy(heap, text, n + 1);
  put_ptr(str, heap);
  put_ptr(str + rc_off("efd_str_size"), heap + n);
  put_ptr(str + rc_off("efd_str_inline"), heap + n + 1);
}
// A movie object: its first word, and its URI (null: none, the string empty)
static uint8_t *menu_movie(const char *name) {
  uint8_t *m = game_alloc(0x200);
  put_ptr(m, vtable_of_movies);
  put_string(m + rc_off("menu_movie_uri"), name ? name : "");
  return m;
}
static void mid_asked(const void *movie, uint32_t lo, uint32_t hi) {
  uint32_t fl = 0, words[2] = {lo, hi};
  set_frame();
#ifdef RC_GUEST32
  ctx->r[6] = as_game(movie);
  memcpy(MID_FRAME + 8, words, 8);
#else
  ctx->r[7] = as_game(movie);
  ctx->r[6] = as_game(words);
#endif
  CHECK_EQ((uint32_t)rc_mid_call(M_ASKED, ctx, &fl), 0);
}
static void mid_locate(void) {
  uint32_t fl = 0;
  set_frame();
  CHECK_EQ((uint32_t)rc_mid_call(M_LOCATE, ctx, &fl), 0);
}
static void mid_located(int accepted) {
  uint32_t fl = 0;
  set_frame();
  ctx->r[0] = 0x55aa5500u | (uint32_t)(accepted ? 1 : 0);  // al is the answer
  CHECK_EQ((uint32_t)rc_mid_call(M_LOCATED, ctx, &fl), 0);
}
// the indexing thread's: a file was indexed, its name and extension in the writer's local
static void mid_indexed(const char *file) {
  uint32_t fl = 0;
  set_frame();
  put_string(MID_FRAME + rc_off("file_index_name"), file);
  CHECK_EQ((uint32_t)rc_mid_call(M_INDEXED, ctx, &fl), 0);
}
// the request as the game makes it: asked, the locator called, answered
static void request_mids(const void *movie, uint32_t lo, uint32_t hi, int accepted) {
  mid_asked(movie, lo, hi);
  mid_locate();
  mid_located(accepted);
}
static void game_request(const void *movie, uint32_t lo, uint32_t hi) {
  request_calls++;
  request_movie = movie;
  request_id[0] = lo, request_id[1] = hi;
  request_mids(movie, lo, hi, locator_accepts);
}
static int frames_ask(void) {  // the requests a frame makes
  int before = request_calls;
  call_frame();
  return request_calls - before;
}

static void menu_retry(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  mid_frame = game_alloc(0x800);
  vtable_of_movies = game_alloc(16);
  uint8_t *movie = menu_movie("urn:application:x-shockwave-flash:titlescreen");
  // the game's request: the locator says no. Nothing is asked again before the file is indexed
  log_start();
  request_mids(movie, 7, 9, 0);
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] menu load: 'titlescreen' was rejected (not indexed yet); waiting for the "
                        "indexer"), 1);
  CHECK_EQ(frames_ask(), 0);
  mid_indexed("cache.xml");
  mid_indexed("titlescreen2.gfx");  // a name that goes on
  mid_indexed("titlescre.gfx");
  CHECK_EQ(frames_ask(), 0);
  // its file is indexed (any case, any extension): the next frame asks once, with the id the game gave, and the
  // movie goes with its answer. In Learn & Play's form the movie is in eax and the id's words are on the stack.
  request_calls = 0;
  locator_accepts = 1;
  mid_indexed("TitleScreen.GFX");
  log_start();
  CHECK_EQ(frames_ask(), 1);
  log_stop();
  CHECK(request_movie == movie);
  CHECK(request_id[0] == 7 && request_id[1] == 9);
  CHECK_EQ(logged_lines("[rs-mods] menu load: 'titlescreen' is indexed now; asking again (request 2)"), 1);
  CHECK_EQ(logged_lines("[rs-mods] menu load: 'titlescreen' accepted after 2 requests"), 1);
  CHECK_EQ(frames_ask(), 0);  // accepted: it is not in the table, and a later file of its name asks nothing
  mid_indexed("titlescreen.gfx");
  CHECK_EQ(frames_ask(), 0);

  // refused again: it waits for the next file of that name, once each
  uint8_t *other = menu_movie("urn:application:x-shockwave-flash:mainmenu");
  request_mids(other, 1, 2, 0);
  locator_accepts = 0;
  mid_indexed("mainmenu.gfx");
  log_start();
  CHECK_EQ(frames_ask(), 1);
  CHECK_EQ(frames_ask(), 0);
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] menu load: 'mainmenu' rejected again (request 2)"), 1);
  locator_accepts = 1;
  mid_indexed("MAINMENU.gfx");
  log_start();
  CHECK_EQ(frames_ask(), 1);
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] menu load: 'mainmenu' accepted after 3 requests"), 1);

  // two movies at once, one file: only its movie is asked
  uint8_t *a = menu_movie("urn:application:x-shockwave-flash:a");
  uint8_t *b = menu_movie("urn:application:x-shockwave-flash:b");
  request_mids(a, 3, 0, 0);
  request_mids(b, 4, 0, 0);
  mid_indexed("b.gfx");
  CHECK_EQ(frames_ask(), 1);
  CHECK(request_movie == b);
  mid_indexed("a.gfx");
  CHECK_EQ(frames_ask(), 1);
  CHECK(request_movie == a);

  // a movie the game let go (its first word is not what it was): not asked, and out of the table
  uint8_t *gone = menu_movie("urn:application:x-shockwave-flash:gone");
  request_mids(gone, 5, 0, 0);
  memset(gone, 0xdd, 16);
  mid_indexed("gone.gfx");
  CHECK_EQ(frames_ask(), 0);
  put_ptr(gone, vtable_of_movies);
  mid_indexed("gone.gfx");
  CHECK_EQ(frames_ask(), 0);
  // ... or whose name is another by the time (the object's memory used again)
  uint8_t *reused = menu_movie("urn:application:x-shockwave-flash:old");
  request_mids(reused, 6, 0, 0);
  put_string(reused + rc_off("menu_movie_uri"), "urn:application:x-shockwave-flash:new");
  mid_indexed("old.gfx");
  CHECK_EQ(frames_ask(), 0);

  // a request that made no locator call (no URI, a movie with a name that was not asked for): nothing is kept or
  // asked
  uint8_t *none = menu_movie(0);
  mid_asked(none, 8, 0);
  mid_located(0);
  uint8_t *quiet = menu_movie("urn:application:x-shockwave-flash:quiet");
  mid_asked(quiet, 8, 0);
  mid_located(0);  // no mid_locate before: not an answer
  mid_indexed("quiet.gfx");
  CHECK_EQ(frames_ask(), 0);

  // RS_MODS_MENU_RETRY=0: the original, nothing kept
  setenv("RS_MODS_MENU_RETRY", "0", 1);
  rs_mods_reload_config();
  uint8_t *off = menu_movie("urn:application:x-shockwave-flash:off");
  request_mids(off, 9, 0, 0);
  mid_indexed("off.gfx");
  CHECK_EQ(frames_ask(), 0);
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  mid_indexed("off.gfx");
  CHECK_EQ(frames_ask(), 0);  // (it was not kept while the switch was off)

  // the file is never seen indexed: asked again after 5 s, once for each 5 s
  uint8_t *late = menu_movie("urn:application:x-shockwave-flash:late");
  request_mids(late, 10, 0, 0);
  CHECK_EQ(frames_ask(), 0);
  usleep(5100000);
  locator_accepts = 1;
  log_start();
  CHECK_EQ(frames_ask(), 1);
  log_stop();
  CHECK(request_movie == late);
  CHECK_EQ(logged_lines("[rs-mods] menu load: WARNING 'late' still rejected and its file was not seen being indexed; "
                        "asking again anyway (fallback 1 of 6)"), 1);
  CHECK_EQ(frames_ask(), 0);
  no_switches();
  rs_mods_reload_config();
}

// Learn a Song with more than five arrangements: the page, the nodes of its movie definition and the game's functions
// that build and move them, as far as the scroll uses them. The builder runs the mid points as the game's does, over
// the song's records; a focus step runs its own (the menu class's) and moves the focus by the steps it is left with.
#define NNODES 96
typedef struct {
  uint8_t *o, *parent;
  char name[40];
  int attached, refs;
} node_t;
static node_t nodes[NNODES];
static int nnodes;
static char ops[4096];
static char key_names[16][48];
static int nkeys, song_n, song_sel, focus_pos, built_slots, builds;
static char labels[8][64];

static void op(const char *fmt, ...) {
  va_list ap;
  size_t n = strlen(ops);
  va_start(ap, fmt);
  vsnprintf(ops + n, sizeof ops - n, fmt, ap);
  va_end(ap);
  strcat(ops, " ");
}
static void put_word(uint8_t *at, uint64_t w) { memcpy(at, &w, (size_t)rc_guest_ptr_size); }
static uint64_t get_word(const uint8_t *at) {
  uint64_t w = 0;
  memcpy(&w, at, (size_t)rc_guest_ptr_size);
  return w;
}
static node_t *node_new(uint8_t *parent, const char *name) {
  CHECK(nnodes < NNODES);
  node_t *n = &nodes[nnodes++];
  n->o = game_alloc(16);
  put_ptr(n->o, data + D_NODE_VTABLE);
  n->parent = parent;
  snprintf(n->name, sizeof n->name, "%s", name);
  n->attached = parent != NULL;
  n->refs = 1;
  return n;
}
static node_t *node_of(const uint8_t *o) {
  for (int k = 0; k < nnodes; k++)
    if (nodes[k].o == o) return &nodes[k];
  return NULL;
}
static node_t *child_of(const uint8_t *node, const char *name) {
  for (int k = 0; k < nnodes; k++)
    if (nodes[k].attached && nodes[k].parent == node && !strcmp(nodes[k].name, name)) return &nodes[k];
  return NULL;
}
static const char *key_name(const uint8_t *key) {
  uint64_t id = get_word(key);
  CHECK(id && id <= (uint64_t)nkeys);
  return key_names[id - 1];
}
// The text of a game string (in place, or behind a pointer at its start)
static void gs_text(const uint8_t *str, char *out) {
  const uint8_t *end = str + rc_off("efd_str_size"), *inl = str + rc_off("efd_str_inline");
  strcpy(out, get_ptr(inl) == end ? (const char *)str : (const char *)get_ptr(str));
}

static void *g_find(uint8_t *node, const char *name) {
  node_t *n = child_of(node, name);
  return n ? n->o : NULL;
}
static int g_remove(uint8_t *node, uint8_t *key) {
  const char *name = key_name(key);
  node_t *n = child_of(node, name);
  op("remove:%s", name);
  if (n) n->attached = 0;
  return n != NULL;
}
static void g_set(uint8_t *node, uint8_t *key, uint8_t *value) {
  node_t *n = node_of(get_ptr(value));
  CHECK(n && !n->attached);  // a node is given to one parent
  op("set:%s", key_name(key));
  n->parent = node;
  snprintf(n->name, sizeof n->name, "%s", key_name(key));
  n->attached = 1;
}
static void g_key(const char *text, uint8_t *out) {
  CHECK(text && out && nkeys < 16);
  snprintf(key_names[nkeys++], sizeof key_names[0], "%s", text);
  put_word(out, (uint64_t)nkeys);
}
static uint8_t *merge_target;
static void g_merge(uint8_t *target, uint8_t *source, uint32_t replace) {
  CHECK(target && source && target != source && replace == 1);
  merge_target = target;
  op("merge");
}
static void g_ref(uint8_t *node, int add) {
  node_t *n = node_of(node);
  CHECK(n);
  n->refs += add ? 1 : -1;
  op(add ? "ref+" : "ref-");
}
static void g_dtor(uint8_t *record) {
  CHECK(record);
  op("dtor");
}

// the points the way the game reaches them: registers and locals as the tables of this test have them
static int run_mid(int point) {
  uint32_t fl = 0;
  return rc_mid_call(point, ctx, &fl);
}
static int mid_alts(uint8_t *page, uint32_t filled, uint32_t record_offset) {
#ifdef RC_GUEST32
  ctx->r[3] = as_game(page);
  ctx->r[4] = esp0 - 1024;
  wr((uint32_t)ctx->r[4] + 0x6c, filled);
  wr((uint32_t)ctx->r[4] + 0x44, record_offset);
#else
  ctx->r[12] = as_game(page);
  ctx->r[13] = record_offset;
  ctx->r[5] = as_game(MID_FRAME);
  memcpy(MID_FRAME - 0x348, &filled, 4);
#endif
  return run_mid(M_ALT);
}
static void mid_page(int point, uint8_t *page) {  // the points that have the page in one register
#ifdef RC_GUEST32
  ctx->r[point == M_ROOT ? 3 : 7] = as_game(page);
#else
  ctx->r[point == M_ROOT ? 15 : 13] = as_game(page);
#endif
  CHECK_EQ(run_mid(point), 0);
}

static void set_records(uint8_t *vec, uint32_t n) {
  uint8_t *buf = game_alloc(n * OFF("arr_record_size") + 1);
  put_ptr(vec, buf);
  put_ptr(vec + rc_guest_ptr_size, buf + n * OFF("arr_record_size"));
}
static uint32_t records_of(const uint8_t *vec) {
  return (uint32_t)((get_word(vec + rc_guest_ptr_size) - get_word(vec)) / OFF("arr_record_size"));
}
static void show_focus(uint8_t *page) {
  char text[64];
  snprintf(text, sizeof text, "Buttons.altArrangementsP1_slot%d.States", focus_pos + 1);
  put_string(page + OFF("menu_page_focus"), text);
}

// The page's builder: a new root and page node with the recommendation buttons (without their States), the root's
// point, then each record but the selected one through the loop's point; a slot for each that goes on, its Label
static void g_build(uint8_t *page) {
  uint64_t sv[16];
  memcpy(sv, ctx->r, sizeof sv);
  op("build");
  builds++;
  node_t *root = node_new(NULL, "root"), *pn = node_new(root->o, "page");
  put_ptr(page + OFF("menu_page_root"), root->o);
  put_ptr(page + OFF("menu_page_node"), pn->o);
  for (int k = 1; k <= 3; k++) {
    char name[32];
    snprintf(name, sizeof name, "playnext_slot%d", k);
    node_new(pn->o, name);
  }
  mid_page(M_ROOT, page);
  if (!records_of(page + OFF("las_page_records"))) set_records(page + OFF("las_page_records"), (uint32_t)song_n);
  uint32_t selected = (uint32_t)song_sel;
  memcpy(page + OFF("las_page_selected"), &selected, 4);
  built_slots = 0;
  memset(labels, 0, sizeof labels);
  for (uint32_t i = 0; i < (uint32_t)song_n; i++) {
    if (i == selected) continue;
    if (mid_alts(page, (uint32_t)built_slots, i * OFF("arr_record_size"))) continue;  // to the loop's latch
    uint8_t *str = game_alloc(0x20);
    char name[16];
    snprintf(name, sizeof name, "Arr%u", i);
    put_string(str, name);
#ifdef RC_GUEST32
    ctx->r[7] = as_game(str);
#else
    ctx->r[1] = as_game(str);
#endif
    CHECK_EQ(run_mid(M_LABEL), 0);
#ifdef RC_GUEST32
    gs_text(as_host((uint32_t)ctx->r[7]), labels[built_slots]);
#else
    gs_text(as_host(ctx->r[1]), labels[built_slots]);
#endif
    snprintf(name, sizeof name, "altArrangementsP1_slot%d", built_slots + 1);
    node_new(pn->o, name);
    built_slots++;
  }
  memcpy(ctx->r, sv, sizeof sv);
}

// The menu class's focus step: its point first, with the page and the steps; the focus moves by the steps it is left with
static int is_coop(const uint8_t *page) { return get_ptr(page) == data + D_COOP_VTABLE; }
static size_t gstr_size(void) { return (size_t)rc_off("efd_str_inline") + (size_t)rc_guest_ptr_size; }
static uint8_t *ids_of(const uint8_t *page) { return (uint8_t *)page + OFF("menu_page_focus_lists") + 3 * rc_guest_ptr_size; }
static int ids_count(const uint8_t *page) {
  return (int)((get_word(ids_of(page) + rc_guest_ptr_size) - get_word(ids_of(page))) / gstr_size());
}
static void id_text(const uint8_t *page, int pos, char *out) {
  gs_text((const uint8_t *)as_host(get_word(ids_of(page)) + (uint64_t)pos * gstr_size()), out);
}
static void g_focus(uint8_t *page, int steps, int down) {
  uint64_t sv[16];
  memcpy(sv, ctx->r, sizeof sv);
  op(down ? "down" : "up");
#ifdef RC_GUEST32
  int saved;
  memcpy(&saved, MID_FRAME + 8, 4);
  ctx->r[7] = as_game(page);
  ctx->r[5] = as_game(MID_FRAME);
  memcpy(MID_FRAME + 8, &steps, 4);
  CHECK_EQ(run_mid(down ? M_FDOWN : M_FUP), 0);
  memcpy(&steps, MID_FRAME + 8, 4);
  memcpy(MID_FRAME + 8, &saved, 4);
#else
  ctx->r[down ? 13 : 3] = as_game(page);
  ctx->r[12] = (uint64_t)(uint32_t)steps;
  CHECK_EQ(run_mid(down ? M_FDOWN : M_FUP), 0);
  steps = (int)(uint32_t)ctx->r[12];
#endif
  memcpy(ctx->r, sv, sizeof sv);
  focus_pos += down ? steps : -steps;
  if (is_coop(page)) {
    char id[64];
    focus_pos = focus_pos < 0 ? 0 : focus_pos > ids_count(page) - 1 ? ids_count(page) - 1 : focus_pos;
    id_text(page, focus_pos, id);
    put_string(page + OFF("menu_page_focus"), id);
    return;
  }
  focus_pos = focus_pos < 0 ? 0 : focus_pos > built_slots - 1 ? built_slots - 1 : focus_pos;
  show_focus(page);
}

// The co-op list's builder: every record's SortOrder through the point (its position, here the record index, as the
// row), then the movie orders its focus ids by row into the lists the page has at the time (the spare ones in a rebuild)
static double coop_rows[32];
static double mid_coop_sort(uint8_t *page, uint32_t index, double row) {
#ifdef RC_GUEST32
  ctx->r[4] = esp0 - 1024;
  wr((uint32_t)ctx->r[4] + 0x38, index);
  wr((uint32_t)ctx->r[4] + 0x18, (uint32_t)as_game(page));
  ctx->fpu.top = 0;
  ctx->fpu.st[0] = row;
  CHECK_EQ(run_mid(M_CSORT), 0);
  return ctx->fpu.st[ctx->fpu.top & 7];
#else
  uint64_t pg = as_game(page);
  ctx->r[13] = index;
  ctx->r[5] = as_game(MID_FRAME);
  memcpy(MID_FRAME - 0x2e8, &pg, 8);
  memcpy(&ctx->x[0], &row, 8);
  CHECK_EQ(run_mid(M_CSORT), 0);
  memcpy(&row, &ctx->x[0], 8);
  return row;
#endif
}
static uint32_t mid_coop_select(uint8_t *page, uint32_t record) {
#ifdef RC_GUEST32
  ctx->r[1] = record;
  ctx->r[3] = as_game(page);
  CHECK_EQ(run_mid(M_CSEL), 0);
  return (uint32_t)ctx->r[1];
#else
  ctx->r[0] = record;
  ctx->r[14] = as_game(page);
  CHECK_EQ(run_mid(M_CSEL), 0);
  return (uint32_t)ctx->r[0];
#endif
}
static void g_cbuild(uint8_t *page) {
  uint64_t sv[16];
  memcpy(sv, ctx->r, sizeof sv);
  op("cbuild");
  builds++;
  uint8_t *rec = page + OFF("arr_coop_records");
  if (!records_of(rec)) set_records(rec, (uint32_t)song_n);
  uint32_t n = records_of(rec), order[32];
  CHECK(n <= 32);
  for (uint32_t i = 0; i < n; i++) {
    coop_rows[i] = mid_coop_sort(page, i, (double)i);
    order[i] = i;
  }
  for (uint32_t i = 1; i < n; i++)  // by row, then by record
    for (uint32_t j = i; j > 0 && coop_rows[order[j]] < coop_rows[order[j - 1]]; j--) {
      uint32_t t = order[j];
      order[j] = order[j - 1], order[j - 1] = t;
    }
  uint8_t *ids = game_alloc((uint32_t)(n * gstr_size()));
  for (uint32_t i = 0; i < n; i++) {
    char id[48];
    snprintf(id, sizeof id, "Buttons.arrangement%u.States", order[i]);
    put_string(ids + i * gstr_size(), id);
  }
  put_ptr(ids_of(page), ids);
  put_ptr(ids_of(page) + rc_guest_ptr_size, ids + n * gstr_size());
  put_ptr(ids_of(page) + 2 * rc_guest_ptr_size, ids + n * gstr_size());
  memcpy(ctx->r, sv, sizeof sv);
}

// The page as the game opens it: a page update (its point), the builder, the States nodes of the recommendations
static uint8_t *open_page(int records, int selected, uint8_t *vtable) {
  uint8_t *page = game_alloc(0x800);
  put_ptr(page, vtable);
  song_n = records, song_sel = selected;
  mid_page(M_OPEN, page);
  g_build(page);
  node_t *pn = node_of(get_ptr(page + OFF("menu_page_node")));
  for (int k = 1; k <= 3; k++) {
    char name[32];
    snprintf(name, sizeof name, "playnext_slot%d", k);
    node_new(child_of(pn->o, name)->o, "States");
  }
  focus_pos = 0;
  show_focus(page);
  return page;
}
static void check_labels(const char *a, const char *b, const char *c, const char *d) {
  if (strcmp(labels[0], a) || strcmp(labels[1], b) || strcmp(labels[2], c) || strcmp(labels[3], d))
    log_end(), fprintf(stderr, "labels [%s] [%s] [%s] [%s], not [%s] [%s] [%s] [%s]\n", labels[0], labels[1], labels[2], labels[3], a,
            b, c, d);
  CHECK(!strcmp(labels[0], a) && !strcmp(labels[1], b) && !strcmp(labels[2], c) && !strcmp(labels[3], d));
}
// The window moves: Down on the last shown alternate scrolls, the entries are rebuilt with " (-N)" / " (+N)" on the
// first / last, and the focus ends where it was; the rest is the game's step.
static void alt_scroll(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  if (!mid_frame) mid_frame = game_alloc(0x800);
  const uint32_t fl = 0x40;
  (void)fl;
  log_start();
  uint8_t *a = open_page(8, 0, data + D_LAS_VTABLE);  // seven alternates, four of them shown
  CHECK_EQ(built_slots, 4);
  check_labels("Arr1", "Arr2", "Arr3", "Arr4 (+3)");
  CHECK_EQ(records_of(a + OFF("las_page_records")), 8);

  // Down on slot 4: scrolls one, the focus returns to slot 4 through one step up and one down
  focus_pos = 3;
  show_focus(a);
  ops[0] = 0;
  builds = 0;
  g_focus(a, 1, 1);
  CHECK_EQ(builds, 1);
  CHECK_EQ(focus_pos, 3);
  check_labels("Arr2 (-1) ", "Arr3 ", "Arr4 ", "Arr5 (+2) ");
  CHECK(!strcmp(ops, "down dtor dtor dtor dtor dtor dtor dtor dtor remove:altArrangementsP1_slot1 "
                     "remove:altArrangementsP1_slot2 remove:altArrangementsP1_slot3 remove:altArrangementsP1_slot4 "
                     "ref+ remove:States ref+ remove:States ref+ remove:States ref+ build merge ref- set:States ref- "
                     "set:States ref- set:States ref- up down "));
  CHECK(merge_target == get_ptr(a + OFF("menu_page_root")));
  CHECK(child_of(get_ptr(a + OFF("menu_page_node")), "playnext_slot2") &&
        child_of(child_of(get_ptr(a + OFF("menu_page_node")), "playnext_slot2")->o, "States"));  // given to the new buttons
  CHECK_EQ(records_of(a + OFF("las_page_records")), 8);
  // down twice more reaches the end (seven alternates: the window starts at 3 at most); then the step is the game's
  focus_pos = 3;
  show_focus(a);
  g_focus(a, 1, 1);
  check_labels("Arr3 (-2) ", "Arr4 ", "Arr5 ", "Arr6 (+1) ");
  g_focus(a, 1, 1);
  check_labels("Arr4 (-3) ", "Arr5 ", "Arr6 ", "Arr7 ");
  CHECK_EQ(focus_pos, 3);
  builds = 0;
  g_focus(a, 1, 1);  // no more alternates below: the game's step, which stops at the end
  CHECK_EQ(builds, 0);
  CHECK_EQ(focus_pos, 3);
  // Up on slot 3 is the game's step; on slot 1 it scrolls back, and the focus is slot 1 again
  g_focus(a, 1, 0);
  CHECK_EQ(builds, 0);
  CHECK_EQ(focus_pos, 2);
  focus_pos = 0;
  show_focus(a);
  ops[0] = 0;
  g_focus(a, 1, 0);
  CHECK_EQ(builds, 1);
  CHECK_EQ(focus_pos, 0);
  check_labels("Arr3 (-2) ", "Arr4 ", "Arr5 ", "Arr6 (+1) ");
  CHECK(strstr(ops, "build merge") && !strncmp(ops, "up ", 3) && !strcmp(ops + strlen(ops) - strlen("up down up "), "up down up "));

  // a page opened anew starts at the top: the update's point, with records already there, builds it again
  uint8_t *d = game_alloc(0x800);
  put_ptr(d, data + D_LAS_VTABLE);
  song_n = 8, song_sel = 0;
  g_build(d);
  check_labels("Arr3 (-2)", "Arr4", "Arr5", "Arr6 (+1)");  // the window is where the last page left it
  builds = 0;
  mid_page(M_OPEN, d);
  CHECK_EQ(builds, 1);
  check_labels("Arr1 ", "Arr2 ", "Arr3 ", "Arr4 (+3) ");
  builds = 0;
  mid_page(M_OPEN, d);  // the same page, updated again at once: nothing
  CHECK_EQ(builds, 0);

  // five arrangements or fewer: nothing to scroll; the focus steps are the game's, whatever slot they are on
  uint8_t *e = open_page(5, 2, data + D_LAS_VTABLE);
  CHECK_EQ(built_slots, 4);
  check_labels("Arr0", "Arr1", "Arr3", "Arr4");
  focus_pos = 3;
  show_focus(e);
  builds = 0;
  g_focus(e, 1, 1);
  CHECK_EQ(builds, 0);
  CHECK_EQ(focus_pos, 3);
  // another page class is not ours
  uint8_t *o = open_page(8, 0, data + D_OTHER_VTABLE);
  focus_pos = 3;
  show_focus(o);
  builds = 0;
  g_focus(o, 1, 1);
  CHECK_EQ(builds, 0);
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song: a song with more than five arrangements shows four alternates at a "
                        "time; the arrow keys and the mouse wheel scroll the rest"), 1);  // said once
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song: Down, window 0 -> 1 of 7 alternates"), 1);
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song: Down, window 3 -> 3 of 7 alternates"), 1);

  // RS_MODS_ARRANGEMENTS=0 is the original: the builder takes every alternate it is given, the page nothing
  setenv("RS_MODS_ARRANGEMENTS", "0", 1);
  rs_mods_reload_config();
  for (uint32_t filled = 0; filled < 7; filled++) CHECK_EQ(mid_alts(a, filled, OFF("arr_record_size")), 0);
  focus_pos = 3;
  show_focus(a);
  builds = 0;
  g_focus(a, 1, 1);
  CHECK_EQ(builds, 0);
  no_switches();
  rs_mods_reload_config();
}

// The co-op arrangement list: more records than rows: the window over the selected record, the rows, the focus steps
// by record through a list that is not in record order, and Enter naming the focused record.
static int coop_focus_record(const uint8_t *page) {
  char focus[64];
  gs_text((const uint8_t *)page + OFF("menu_page_focus"), focus);
  return rs_mods_coop_focus_index(focus);
}
static void coop_focus_on(uint8_t *page, int record) {
  char id[64], want[64];
  snprintf(want, sizeof want, "Buttons.arrangement%d.States", record);
  for (int k = 0; k < ids_count(page); k++) {
    id_text(page, k, id);
    if (!strcmp(id, want)) focus_pos = k;
  }
  id_text(page, focus_pos, id);
  put_string(page + OFF("menu_page_focus"), id);
}
static void coop_order(const uint8_t *page, int *out) {  // the records in the order of the page's own focus id list
  char id[64];
  for (int k = 0; k < ids_count(page); k++) id_text(page, k, id), out[k] = rs_mods_coop_focus_index(id);
}
static void alt_coop(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  log_start();
  // twelve records, the tenth selected: the window starts at 3, records 3..10 take rows 0..7, the others a row below
  uint8_t *page = game_alloc(0x800);
  uint32_t sel = 10;
  put_ptr(page, data + D_COOP_VTABLE);
  memcpy(page + OFF("arr_coop_selected"), &sel, 4);
  song_n = 12;
  g_cbuild(page);
  for (int i = 0; i < 12; i++) CHECK_EQ((int)coop_rows[i], i >= 3 && i <= 10 ? i - 3 : 100 + i);
  int order[16], want_entry[12] = {3, 4, 5, 6, 7, 8, 9, 10, 0, 1, 2, 11};
  coop_order(page, order);
  CHECK(!memcmp(order, want_entry, sizeof want_entry));
  // Enter on record 10: the game's map gives the list position (7: the 8th entry), the focused entry's name the record
  coop_focus_on(page, 10);
  CHECK_EQ(focus_pos, 7);
  CHECK_EQ(mid_coop_select(page, (uint32_t)focus_pos), 10);
  CHECK_EQ(mid_coop_select(page, 10), 10);
  // Down: the target is record 11, the window moves to 4, the list stays as it was, the focus is record 11 (position 11)
  ops[0] = 0;
  builds = 0;
  g_focus(page, 1, 1);
  CHECK_EQ(builds, 1);
  CHECK_EQ(coop_focus_record(page), 11);
  // the step the game was given is 0 steps long: ours is the second; the builder runs after its records are destroyed
  CHECK(!strcmp(ops, "down dtor dtor dtor dtor dtor dtor dtor dtor dtor dtor dtor dtor cbuild down "));
  for (int i = 0; i < 12; i++) CHECK_EQ((int)coop_rows[i], i >= 4 && i <= 11 ? i - 4 : 100 + i);
  coop_order(page, order);
  CHECK(!memcmp(order, want_entry, sizeof want_entry));  // the page's own focus lists, not the builder's
  // Up: the target is record 10, which is in the window: no rebuild, four positions back
  builds = 0;
  ops[0] = 0;
  g_focus(page, 1, 0);
  CHECK_EQ(builds, 0);
  CHECK_EQ(coop_focus_record(page), 10);
  CHECK(!strcmp(ops, "up up "));
  // up to record 3, then once more: record 2 is before the window (moves to 2), eight positions the other way
  for (int r = 9; r >= 3; r--) {
    g_focus(page, 1, 0);
    CHECK_EQ(coop_focus_record(page), r);
  }
  builds = 0;
  g_focus(page, 1, 0);
  CHECK_EQ(builds, 1);
  CHECK_EQ(coop_focus_record(page), 2);
  for (int i = 0; i < 12; i++) CHECK_EQ((int)coop_rows[i], i >= 2 && i <= 9 ? i - 2 : 100 + i);
  // eight records or fewer fit: the steps are the game's, no window, rows as the builder has them
  uint8_t *small = game_alloc(0x800);
  sel = 3;
  put_ptr(small, data + D_COOP_VTABLE);
  memcpy(small + OFF("arr_coop_selected"), &sel, 4);
  song_n = 8;
  g_cbuild(small);
  for (int i = 0; i < 8; i++) CHECK_EQ((int)coop_rows[i], i);
  coop_focus_on(small, 2);
  builds = 0;
  ops[0] = 0;
  g_focus(small, 1, 1);
  CHECK_EQ(builds, 0);
  CHECK_EQ(coop_focus_record(small), 3);
  CHECK(!strcmp(ops, "down "));
  // a second player's page has a window of its own (the small one's page is gone: its first word is not the list's now)
  put_ptr(small, NULL);
  uint8_t *other = game_alloc(0x800);
  sel = 0;
  put_ptr(other, data + D_COOP_VTABLE);
  memcpy(other + OFF("arr_coop_selected"), &sel, 4);
  song_n = 12;
  g_cbuild(other);
  for (int i = 0; i < 12; i++) CHECK_EQ((int)coop_rows[i], i < 8 ? i : 100 + i);
  coop_focus_on(page, 5);  // the first page's window is untouched by the second's build
  g_focus(page, 1, 1);
  CHECK_EQ(coop_focus_record(page), 6);
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song: co-op list scrolled to 4 of 12"), 1);
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song: co-op list scrolled to 2 of 12"), 1);
  CHECK_EQ(logged_lines("[rs-mods] Learn a Song: co-op select Buttons.arrangement10.States -> record 10 (the list "
                        "position gave 7)"), 1);
  setenv("RS_MODS_ARRANGEMENTS", "0", 1);  // the original: the point leaves the rows and the record alone
  rs_mods_reload_config();
  song_n = 12;
  uint8_t *orig = game_alloc(0x800);
  sel = 10;
  put_ptr(orig, data + D_COOP_VTABLE);
  memcpy(orig + OFF("arr_coop_selected"), &sel, 4);
  g_cbuild(orig);
  for (int i = 0; i < 12; i++) CHECK_EQ((int)coop_rows[i], i);
  CHECK_EQ(mid_coop_select(orig, 7), 7);
  no_switches();
  rs_mods_reload_config();
}


// The window arithmetic of the scrollable arrangement lists
static void alt_window(void) {
  // selected 2 of 9 records: alternates are records 0,1,3,4,5,6,7,8 (ranks 0..7)
  int shown[9] = {0};
  for (uint32_t i = 0; i < 9; i++) shown[i] = rs_mods_alt_in_window(i, 2, 0);
  CHECK(shown[0] && shown[1] && !shown[2] && shown[3] && shown[4] && !shown[5] && !shown[8]);
  CHECK(rs_mods_alt_in_window(8, 2, 4) && !rs_mods_alt_in_window(0, 2, 1) && rs_mods_alt_in_window(1, 2, 1));
  CHECK_EQ(rs_mods_alt_scroll(0, 8, 1, 0), 1);
  CHECK_EQ(rs_mods_alt_scroll(4, 8, 1, 0), 4);  // the window ends at the last alternate
  CHECK_EQ(rs_mods_alt_scroll(0, 8, 0, 1), 0);
  CHECK_EQ(rs_mods_alt_scroll(3, 8, 0, 1), 2);
  CHECK_EQ(rs_mods_alt_scroll(0, 4, 1, 0), 0);  // four alternates: nothing to scroll
  char t[16];
  rs_mods_alt_label_tag(1, 0, 8, t, sizeof t); CHECK_EQ(strcmp(t, ""), 0);
  rs_mods_alt_label_tag(4, 0, 8, t, sizeof t); CHECK_EQ(strcmp(t, " (+4)"), 0);
  rs_mods_alt_label_tag(1, 2, 8, t, sizeof t); CHECK_EQ(strcmp(t, " (-2)"), 0);
  rs_mods_alt_label_tag(2, 2, 8, t, sizeof t); CHECK_EQ(strcmp(t, ""), 0);
  rs_mods_alt_label_tag(4, 4, 8, t, sizeof t); CHECK_EQ(strcmp(t, ""), 0);
  CHECK_EQ(rs_mods_coop_window_for(0, 8, 7), 0);  // eight rows fit
  CHECK_EQ(rs_mods_coop_window_for(0, 12, 3), 0);
  CHECK_EQ(rs_mods_coop_window_for(0, 12, 8), 1);
  CHECK_EQ(rs_mods_coop_window_for(5, 12, 6), 4);  // 6 is in rows 5..12 but the list ends at 4..11
  CHECK_EQ(rs_mods_coop_window_for(3, 12, 1), 1);
  CHECK_EQ(rs_mods_coop_window_for(0, 12, 11), 4);
  CHECK(rs_mods_coop_in_window(0, 8, 0) && rs_mods_coop_in_window(11, 8, 0));  // nothing to window
  CHECK(!rs_mods_coop_in_window(2, 12, 3) && rs_mods_coop_in_window(3, 12, 3) && rs_mods_coop_in_window(10, 12, 3) &&
        !rs_mods_coop_in_window(11, 12, 3));
  CHECK_EQ(rs_mods_coop_step(3, 12, 1), 4);
  CHECK_EQ(rs_mods_coop_step(0, 12, -1), 0);   // stops at the ends
  CHECK_EQ(rs_mods_coop_step(11, 12, 5), 11);
  CHECK_EQ(rs_mods_coop_step(5, 12, -9), 0);
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.arrangement3.States"), 3);
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.arrangement.States"), -1);
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.back"), -1);
  CHECK_EQ(rs_mods_coop_focus_index(NULL), -1);
}

// The same arithmetic at its edges: no alternates, exactly as many as the slots (no scroll), one more (the smallest
// scroll), the selected record first, last or not a record, the co-op list at exactly RS_MODS_COOP_ROWS and one more;
// then every list length up to twenty, scrolled from top to bottom and back
static void alt_window_edges(void) {
  char t[16];
  // no records, and a song of one arrangement: nothing shown, nothing to scroll, no tags
  CHECK(!rs_mods_alt_in_window(0, 0, 0));
  CHECK_EQ(rs_mods_alt_scroll(0, 0, 1, 0), 0);
  CHECK_EQ(rs_mods_alt_scroll(0, 0, 0, 1), 0);
  for (uint32_t s = 1; s <= 4; s++) {
    rs_mods_alt_label_tag(s, 0, 0, t, sizeof t);
    CHECK_EQ(t[0], 0);
  }
  // five records, four alternates: all shown, whichever is selected; no tag, no scroll
  for (uint32_t sel = 0; sel < 5; sel++)
    for (uint32_t i = 0; i < 5; i++) CHECK_EQ(rs_mods_alt_in_window(i, sel, 0), i != sel);
  CHECK_EQ(rs_mods_alt_scroll(0, 4, 1, 0), 0);
  rs_mods_alt_label_tag(4, 0, 4, t, sizeof t);
  CHECK_EQ(t[0], 0);
  // six records, five alternates, the selected one last: records 0..3, then 1..4
  CHECK(rs_mods_alt_in_window(0, 5, 0) && rs_mods_alt_in_window(3, 5, 0) && !rs_mods_alt_in_window(4, 5, 0));
  rs_mods_alt_label_tag(4, 0, 5, t, sizeof t);
  CHECK_EQ(strcmp(t, " (+1)"), 0);
  CHECK_EQ(rs_mods_alt_scroll(0, 5, 1, 0), 1);
  CHECK(!rs_mods_alt_in_window(0, 5, 1) && rs_mods_alt_in_window(4, 5, 1) && !rs_mods_alt_in_window(5, 5, 1));
  CHECK_EQ(rs_mods_alt_scroll(1, 5, 1, 0), 1);  // the last alternate is in the last slot
  rs_mods_alt_label_tag(1, 1, 5, t, sizeof t);
  CHECK_EQ(strcmp(t, " (-1)"), 0);
  rs_mods_alt_label_tag(4, 1, 5, t, sizeof t);
  CHECK_EQ(t[0], 0);
  CHECK_EQ(rs_mods_alt_scroll(1, 5, 0, 1), 0);
  // the selected one first: the window starts at record 1
  CHECK(!rs_mods_alt_in_window(0, 0, 0) && rs_mods_alt_in_window(1, 0, 0) && rs_mods_alt_in_window(4, 0, 0) &&
        !rs_mods_alt_in_window(5, 0, 0) && rs_mods_alt_in_window(5, 0, 1) && !rs_mods_alt_in_window(1, 0, 1));
  // a selection that is no record: every record is an alternate
  CHECK(rs_mods_alt_in_window(0, 0xffffffffu, 0) && rs_mods_alt_in_window(3, 0xffffffffu, 0) &&
        !rs_mods_alt_in_window(4, 0xffffffffu, 0));
  // both ways at once, and neither: a step one way (down first), or none
  CHECK_EQ(rs_mods_alt_scroll(1, 8, 1, 1), 2);
  CHECK_EQ(rs_mods_alt_scroll(1, 8, 0, 0), 1);
  // a tag in a buffer too small for it is cut, a buffer of none is left alone
  rs_mods_alt_label_tag(4, 0, 9, t, 3);
  CHECK_EQ(strcmp(t, " ("), 0);
  t[0] = 'x';
  rs_mods_alt_label_tag(4, 0, 9, t, 0);
  CHECK_EQ(t[0], 'x');
  rs_mods_alt_label_tag(4, 0, 1000, t, sizeof t);
  CHECK_EQ(strcmp(t, " (+996)"), 0);

  // every song of up to twenty arrangements, any one selected: from the top, Down to the end and Up back. At every
  // position the window shows min(4, the alternates left from there) of them, each alternate shown in exactly one slot
  // position, the tags count what is out of sight, and Down stops with the last alternate in the window
  for (uint32_t count = 0; count <= 20; count++)
    for (uint32_t sel = 0; sel < (count ? count : 1); sel++) {
      uint32_t alts = count ? count - 1 : 0, offset = 0, last = alts > 4 ? alts - 4 : 0, steps = 0;
      for (;;) {
        uint32_t shown = 0;
        for (uint32_t i = 0; i < count; i++) shown += (uint32_t)rs_mods_alt_in_window(i, sel, offset);
        CHECK_EQ(shown, alts - offset < 4 ? alts - offset : 4);
        uint32_t hidden = 0;
        char a[16], b[16];
        rs_mods_alt_label_tag(1, offset, alts, a, sizeof a);
        rs_mods_alt_label_tag(4, offset, alts, b, sizeof b);
        if (a[0]) hidden += (uint32_t)strtoul(a + 3, NULL, 10);
        if (b[0]) hidden += (uint32_t)strtoul(b + 3, NULL, 10);
        CHECK_EQ(hidden, alts - shown);
        CHECK_EQ(a[0] != 0, offset > 0);
        uint32_t next = rs_mods_alt_scroll(offset, alts, 1, 0);
        if (next == offset) break;
        CHECK_EQ(next, offset + 1);
        offset = next, steps++;
        CHECK(steps <= 20);
      }
      CHECK_EQ(offset, last);
      while (offset) {
        uint32_t next = rs_mods_alt_scroll(offset, alts, 0, 1);
        CHECK_EQ(next, offset - 1);
        offset = next;
      }
      CHECK_EQ(rs_mods_alt_scroll(0, alts, 0, 1), 0);
    }

  // the co-op list: none, eight (all shown, no window) and nine records (one row to scroll)
  CHECK_EQ(rs_mods_coop_window_for(0, 0, 0), 0);
  CHECK_EQ(rs_mods_coop_step(0, 0, 1), 0);
  CHECK_EQ(rs_mods_coop_step(0, 0, -1), 0);
  CHECK_EQ(rs_mods_coop_window_for(3, 8, 7), 0);  // a stale offset of a longer list
  for (uint32_t i = 0; i < 8; i++) CHECK(rs_mods_coop_in_window(i, 8, 0));
  CHECK_EQ(rs_mods_coop_window_for(0, 9, 7), 0);
  CHECK_EQ(rs_mods_coop_window_for(0, 9, 8), 1);
  CHECK_EQ(rs_mods_coop_window_for(1, 9, 0), 0);
  CHECK_EQ(rs_mods_coop_window_for(5, 9, 4), 1);  // an offset past the last window is pulled back to it
  CHECK_EQ(rs_mods_coop_window_for(0, 9, 20), 1); // a selection past the end: the last window
  CHECK(!rs_mods_coop_in_window(8, 9, 0) && rs_mods_coop_in_window(8, 9, 1) && !rs_mods_coop_in_window(0, 9, 1));
  CHECK_EQ(rs_mods_coop_step(7, 9, 1), 8);
  CHECK_EQ(rs_mods_coop_step(8, 9, 1), 8);
  CHECK_EQ(rs_mods_coop_step(1, 9, -1), 0);
  CHECK_EQ(rs_mods_coop_step(0, 9, 0), 0);
  // every list of up to twenty records, from every offset, onto every record: the window shows it, moved no further
  // than it has to (not at all when it is shown already), never past the last window
  for (uint32_t count = 1; count <= 20; count++) {
    uint32_t last = count > RS_MODS_COOP_ROWS ? count - RS_MODS_COOP_ROWS : 0;
    for (uint32_t from = 0; from <= count; from++)
      for (uint32_t sel = 0; sel < count; sel++) {
        uint32_t o = rs_mods_coop_window_for(from, count, sel), start = from > last ? last : from;
        CHECK(o <= last && rs_mods_coop_in_window(sel, count, o));
        if (count > RS_MODS_COOP_ROWS && rs_mods_coop_in_window(sel, count, start)) CHECK_EQ(o, start);
        else if (count > RS_MODS_COOP_ROWS) CHECK_EQ(o, sel < start ? sel : sel - RS_MODS_COOP_ROWS + 1);
      }
  }
  // the focused button's record: the digits after "arrangement", and nothing else
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.arrangement12.States"), 12);
  CHECK_EQ(rs_mods_coop_focus_index("arrangement0"), 0);
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.arrangement99999999.States"), -1);
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.altArrangementsP1_slot1.States"), -1);  // Learn a Song's slots: not co-op
  CHECK_EQ(rs_mods_coop_focus_index("Buttons.arrangementX1.States"), -1);
}

int main(void) {
#ifdef RC_GUEST32
  CHECK(!rc32_mem_init());
  ctx = rc32_thread_ctx();
  esp0 = (uint32_t)ctx->r[4];
  obj_guest = rc32_alloc(256);
  obj = RC32_H(obj_guest);
  CHECK(!rc32_mem_commit(DATA, D_SIZE));
  data = RC32_H(DATA);
#else
  static rc_ctx c64;
  static uint8_t image[D_SIZE];
  ctx = &c64;
  obj = calloc(1, 256);
  data = image;
  rc_slide = (uint64_t)(uintptr_t)image - DATA;
#endif
  // the image's data: the ten slots, and the two pointers to the profile keys' text
  binding = (int32_t *)(data + D_BINDING);
  strcpy((char *)data + D_TEXT_DIRECT, "DirectKey");
  strcpy((char *)data + D_TEXT_MIC, "MicKey");
  put_ptr(data + D_KEY_DIRECT, data + D_TEXT_DIRECT);
  put_ptr(data + D_KEY_MIC, data + D_TEXT_MIC);
  put_ptr(data + D_NODE_VTABLE, as_host(ADDR(G_ADDREF)));  // the nodes' reference methods (the 32-bit runtime calls
  put_ptr(data + D_NODE_VTABLE + rc_guest_ptr_size, as_host(ADDR(G_RELEASE)));  // these; the 64-bit test dispatches by slot)
  input_obj = game_alloc(64);
  manager_obj = game_alloc(16);
  text_heap = game_alloc(TEXT_HEAP);
  // the file system objects: in Learn & Play's kind of image each has a vtable with the question's function
  uint8_t *vtable = game_alloc(64);
  put_ptr(vtable + rc_off("fs_exists_slot") * rc_guest_ptr_size, as_host(ADDR(FSEXISTS)));
  fs_whole = game_alloc(16);
  put_ptr(fs_whole, vtable);
  for (int k = 0; k < ARCHIVES; k++) put_ptr(fs_arch[k] = game_alloc(16), vtable);
  switches();
  log_lines();
  start();
  intro();
  ownership();
  saved_input();
  skip_tuner();
  menu_retry();
  alt_scroll();
  alt_coop();
  alt_window();
  alt_window_edges();
  manifests();
  mounting_edges();
  file_questions();
  overlay_names();
  printf("mods_shared: ok (%s)\n", rc_guest_ptr_size == 4 ? "32-bit guest" : "64-bit guest");
  return 0;
}
