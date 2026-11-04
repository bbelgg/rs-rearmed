// The win-x86 Steam stub through the guest ABI, without the game: imports called with their
// arguments on a guest stack, interface methods through their vtable cells (thiscall, callee
// pops), and callbacks into "guest" functions (C functions in rc32_table at fake addresses that
// behave like MSVC thiscall code: `ret 4 * n`).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "steam32.h"
#include "win32_guestobj.h"

// --- fake guest code -----------------------------------------------------------------------
enum { G_RUN_RESULT = 0x1000, G_RUN = 0x1010, G_SIZE = 0x1020, G_CTXINIT = 0x1030 };
int w32_log;  // win32_guestobj.c's log level (win32_ksync.c has it in the game)

static uint32_t rd(uint32_t g) { return w32_rd32(g); }
static void guest_ret(rc_ctx *c, uint32_t eax, int words) {
  c->ret_to = rd((uint32_t)c->r[4]);
  c->r[4] += 4 + 4 * (uint32_t)words;
  c->r[0] = eax;
}
static int runs, results, ctxinits;
static uint32_t last_this, last_failed;
static uint64_t last_handle;
static uint8_t last_payload[64];
static void f_run(rc_ctx *c) {  // Run(void *)
  uint32_t esp = (uint32_t)c->r[4];
  last_this = (uint32_t)c->r[1];
  memcpy(last_payload, RC32_H(rd(esp + 4)), sizeof last_payload);
  runs++;
  guest_ret(c, 0, 1);
}
static void f_run_result(rc_ctx *c) {  // Run(void *, bool, SteamAPICall_t)
  uint32_t esp = (uint32_t)c->r[4];
  last_this = (uint32_t)c->r[1];
  memcpy(last_payload, RC32_H(rd(esp + 4)), sizeof last_payload);
  last_failed = rd(esp + 8) & 0xff;
  last_handle = rd(esp + 12) | (uint64_t)rd(esp + 16) << 32;
  results++;
  guest_ret(c, 0, 4);
}
static void f_size(rc_ctx *c) { guest_ret(c, 24, 0); }
static void f_ctxinit(rc_ctx *c) {  // cdecl void (*)(void *ctx): caller pops
  ctxinits++;
  c->ret_to = rd((uint32_t)c->r[4]);
  c->r[4] += 4;
}
// The runtime's image registry is not linked: no image here, only these "guest" functions,
// which rc32_call finds through this rc32_lookup (and no TLS, no rc32_icall hooks).
static const rc32_entry table[] = {{G_RUN_RESULT, f_run_result}, {G_RUN, f_run}, {G_SIZE, f_size}, {G_CTXINIT, f_ctxinit}};
rc_fn rc32_lookup(uint32_t g) {
  for (size_t k = 0; k < sizeof table / sizeof table[0]; k++)
    if (table[k].addr == g) return table[k].fn;
  return 0;
}
int rc32_tls_images(const rc32_tls_image **out) { *out = 0; return 0; }
void rc32_hostfn_hook_add(rc32_hostfn_hook h) { (void)h; }
_Noreturn void rc_fail(const char *what, uint64_t addr) {
  fprintf(stderr, "rc_fail: %s at 0x%llx\n", what, (unsigned long long)addr);
  abort();
}

// --- calling into the stub as guest code would ----------------------------------------------
static rc_ctx *C;
static uint32_t imp(const char *name, int n, const uint32_t *a, uint32_t *hi) {
  rc_fn f = steam32_import(name);
  if (!f) { fprintf(stderr, "no import %s\n", name); exit(1); }
  uint32_t esp0 = (uint32_t)C->r[4];
  C->r[4] -= 4 * (uint32_t)n;
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)C->r[4] + 4 * k, a[k]);
  f(C);
  assert((uint32_t)C->r[4] == esp0 - 4 * (uint32_t)n);  // cdecl: the caller pops
  C->r[4] = esp0;
  if (hi) *hi = (uint32_t)C->r[2];
  return (uint32_t)C->r[0];
}
// thiscall through the object's vtable; checks the callee removed exactly n words
static uint64_t vcall(uint32_t obj, int slot, int n, const uint32_t *a) {
  uint32_t esp0 = (uint32_t)C->r[4];
  C->r[4] -= 4 * (uint32_t)n + 4;
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)C->r[4] + 4 + 4 * k, a[k]);
  w32_wr32((uint32_t)C->r[4], 0xdead0000u);  // return address
  C->r[1] = obj;
  int ok = w32_hostfn_call(C, rd(rd(obj) + 4 * slot));
  assert(ok);
  assert(C->ret_to == 0xdead0000u);
  assert((uint32_t)C->r[4] == esp0);
  return (uint32_t)C->r[0] | (uint64_t)(uint32_t)C->r[2] << 32;
}
static uint32_t gs(const char *s) { return w32_gstr(s); }
static uint32_t pl32(int off) { uint32_t v; memcpy(&v, last_payload + off, 4); return v; }
static const char *hs(uint32_t g) { return (const char *)RC32_H(g); }

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

int main(void) {
  CHECK(rc32_mem_init() == 0);
  C = rc32_thread_ctx();

  CHECK(imp("steam_api.dll!SteamAPI_RestartAppIfNecessary", 1, (uint32_t[]){221680}, 0) == 0);
  CHECK(imp("STEAM_API.DLL!SteamAPI_Init", 0, 0, 0) == 1);
  CHECK(imp("steam_api.dll!SteamAPI_GetHSteamPipe", 0, 0, 0) == 1);
  CHECK(!steam32_import("steam_api.dll!NoSuchExport") && !steam32_import("kernel32.dll!SteamAPI_Init"));

  // ContextInit: pFn runs once per SteamAPI_Init
  uint32_t blk = w32_galloc(64);
  w32_wr32(blk, G_CTXINIT);
  CHECK(imp("steam_api.dll!SteamInternal_ContextInit", 1, (uint32_t[]){blk}, 0) == blk + 8);
  CHECK(imp("steam_api.dll!SteamInternal_ContextInit", 1, (uint32_t[]){blk}, 0) == blk + 8);
  CHECK(ctxinits == 1);

  // ISteamClient -> ISteamUser
  uint32_t client = imp("steam_api.dll!SteamInternal_CreateInterface", 1, (uint32_t[]){gs("SteamClient019")}, 0);
  CHECK(client && client == imp("steam_api.dll!SteamClient", 0, 0, 0));
  uint32_t user = (uint32_t)vcall(client, 5, 3, (uint32_t[]){1, 1, gs("SteamUser020")});  // GetISteamUser
  CHECK(user && user == imp("steam_api.dll!SteamInternal_FindOrCreateUserInterface", 2, (uint32_t[]){1, gs("SteamUser020")}, 0));
  CHECK(vcall(user, 1, 0, 0) == 1);  // BLoggedOn
  uint32_t sid = w32_galloc(8);
  CHECK(vcall(user, 2, 1, (uint32_t[]){sid}) == sid);  // GetSteamID through the hidden pointer
  uint64_t id;
  memcpy(&id, RC32_H(sid), 8);
  CHECK(id == 76561197960265729ull);
  CHECK(vcall(user, 17, 3, (uint32_t[]){(uint32_t)id, (uint32_t)(id >> 32), 221680}) == 0);  // UserHasLicenseForApp
  CHECK(vcall(user, 3, 7, (uint32_t[]){0, 0, 0, 0, 0, 0, 0}) == 0);  // InitiateGameConnection: not implemented, 7 words

  // ISteamFriends / ISteamUtils
  uint32_t friends = (uint32_t)vcall(client, 8, 3, (uint32_t[]){1, 1, gs("SteamFriends017")});
  CHECK(!strcmp(hs((uint32_t)vcall(friends, 0, 0, 0)), "Player"));  // GetPersonaName
  CHECK(vcall(friends, 11, 2, (uint32_t[]){1, 2}) == 0);              // GetPlayerNickname: NULL
  uint32_t fbi = w32_galloc(8);
  w32_wr64(fbi, ~0ull);
  CHECK(vcall(friends, 4, 3, (uint32_t[]){fbi, 0, 4}) == fbi);  // GetFriendByIndex: a zeroed CSteamID
  memcpy(&id, RC32_H(fbi), 8);
  CHECK(id == 0);
  uint32_t utils = (uint32_t)vcall(client, 9, 2, (uint32_t[]){1, gs("SteamUtils009")});
  CHECK(vcall(utils, 9, 0, 0) == 221680);                                 // GetAppID
  CHECK(!strcmp(hs((uint32_t)vcall(utils, 23, 0, 0)), "english"));        // GetSteamUILanguage

  // ISteamApps: base app + the configured DLC
  uint32_t apps = (uint32_t)vcall(client, 15, 3, (uint32_t[]){1, 1, gs("STEAMAPPS_INTERFACE_VERSION008")});
  CHECK(vcall(apps, 6, 1, (uint32_t[]){221680}) == 1);
  CHECK(vcall(apps, 6, 1, (uint32_t[]){258341}) == 1);
  CHECK(vcall(apps, 6, 1, (uint32_t[]){12345}) == 0);
  CHECK(vcall(apps, 6, 1, (uint32_t[]){206166}) == 1);  // RS_STEAM_OWNED: owned, but no DLC of this game
  CHECK(vcall(apps, 7, 1, (uint32_t[]){206166}) == 0);
  CHECK(vcall(apps, 7, 1, (uint32_t[]){221680}) == 0);  // BIsDlcInstalled: not the base app
  CHECK(vcall(apps, 10, 0, 0) == 2);
  uint32_t out = w32_galloc(16);
  CHECK(vcall(apps, 11, 5, (uint32_t[]){1, out, out + 4, 0, 0}) == 1);
  CHECK(rd(out) == 258341 && (rd(out + 4) & 0xff) == 1);

  // ISteamUserStats: a stat round trip, RequestCurrentStats -> UserStatsReceived_t (1101)
  uint32_t stats = (uint32_t)vcall(client, 13, 3, (uint32_t[]){1, 1, gs("STEAMUSERSTATS_INTERFACE_VERSION011")});
  CHECK(vcall(stats, 4, 2, (uint32_t[]){gs("PlayTime"), 42}) == 1);  // SetStat (int32)
  CHECK(vcall(stats, 2, 2, (uint32_t[]){gs("PlayTime"), out}) == 1 && rd(out) == 42);
  uint32_t vt = w32_galloc(16), cb = w32_galloc(32);
  w32_wr32(vt, G_RUN_RESULT);
  w32_wr32(vt + 4, G_RUN);
  w32_wr32(vt + 8, G_SIZE);
  w32_wr32(cb, vt);
  imp("steam_api.dll!SteamAPI_RegisterCallback", 2, (uint32_t[]){cb, 1101}, 0);
  CHECK(rd(cb + 8) == 1101 && (rd(cb + 4) & 1));
  CHECK(vcall(stats, 0, 0, 0) == 1);
  imp("steam_api.dll!SteamAPI_RunCallbacks", 0, 0, 0);
  CHECK(runs == 1 && last_this == cb);
  uint64_t game;
  memcpy(&game, last_payload, 8);
  memcpy(&id, last_payload + 16, 8);  // pack 8: the CSteamID after padding
  CHECK(game == 221680 && id == 76561197960265729ull);
  imp("steam_api.dll!SteamAPI_RunCallbacks", 0, 0, 0);
  CHECK(runs == 1);  // dispatched once
  imp("steam_api.dll!SteamAPI_UnregisterCallback", 1, (uint32_t[]){cb}, 0);
  vcall(stats, 0, 0, 0);
  imp("steam_api.dll!SteamAPI_RunCallbacks", 0, 0, 0);
  CHECK(runs == 1);

  // the ENUMERATE press: no overlay, and DlcInstalled_t (1005) for the free DLC
  imp("steam_api.dll!SteamAPI_RegisterCallback", 2, (uint32_t[]){cb, 1005}, 0);
  CHECK(vcall(utils, 17, 0, 0) == 0);  // IsOverlayEnabled
  for (int i = 0; i < 500 && runs == 1; i++) {
    imp("steam_api.dll!SteamAPI_RunCallbacks", 0, 0, 0);
    usleep(10000);
  }
  CHECK(runs == 2 && last_this == cb);
  uint32_t dlc;
  memcpy(&dlc, last_payload, 4);
  CHECK(dlc == 248750);
  imp("steam_api.dll!SteamAPI_UnregisterCallback", 1, (uint32_t[]){cb}, 0);

  // ISteamRemoteStorage: write, read, async read with its call result
  uint32_t rs = (uint32_t)vcall(client, 17, 3, (uint32_t[]){1, 1, gs("STEAMREMOTESTORAGE_INTERFACE_VERSION014")});
  uint32_t data = gs("hello cloud");
  CHECK(vcall(rs, 0, 3, (uint32_t[]){gs("dir/test.txt"), data, 11}) == 1);
  CHECK(vcall(rs, 13, 1, (uint32_t[]){gs("dir/test.txt")}) == 1);
  CHECK(vcall(rs, 15, 1, (uint32_t[]){gs("dir/test.txt")}) == 11);
  uint32_t buf = w32_galloc(64);
  CHECK(vcall(rs, 1, 3, (uint32_t[]){gs("dir/test.txt"), buf, 64}) == 11 && !memcmp(RC32_H(buf), "hello cloud", 11));
  CHECK(vcall(rs, 0, 3, (uint32_t[]){gs("../escape"), data, 11}) == 0);
  CHECK(vcall(rs, 16, 1, (uint32_t[]){gs("dir/test.txt")}) > 1000000000ull);  // GetFileTimestamp, edx:eax
  uint64_t h = vcall(rs, 3, 3, (uint32_t[]){gs("dir/test.txt"), 6, 5});      // FileReadAsync
  CHECK(h);
  uint32_t cr = w32_galloc(32);
  w32_wr32(cr, vt);
  imp("steam_api.dll!SteamAPI_RegisterCallResult", 3, (uint32_t[]){cr, (uint32_t)h, (uint32_t)(h >> 32)}, 0);
  imp("steam_api.dll!SteamAPI_RunCallbacks", 0, 0, 0);
  CHECK(results == 1 && last_this == cr && last_handle == h && !last_failed);
  uint64_t call;
  memcpy(&call, last_payload, 8);
  CHECK(call == h && pl32(8) == 1 && pl32(12) == 6 && pl32(16) == 5);  // OK, offset, bytes read
  CHECK(vcall(rs, 4, 4, (uint32_t[]){(uint32_t)h, (uint32_t)(h >> 32), buf, 64}) == 1);  // FileReadAsyncComplete
  CHECK(!memcmp(RC32_H(buf), "cloud", 5));
  CHECK(vcall(rs, 4, 4, (uint32_t[]){(uint32_t)h, (uint32_t)(h >> 32), buf, 64}) == 0);  // taken

  // UserStatsStored_t after StoreStats; GetAPICallResult on a result nobody registered for
  uint64_t wh = vcall(rs, 2, 3, (uint32_t[]){gs("w.bin"), data, 5});  // FileWriteAsync
  CHECK(vcall(utils, 11, 3, (uint32_t[]){(uint32_t)wh, (uint32_t)(wh >> 32), out}) == 1);  // IsAPICallCompleted
  CHECK(vcall(utils, 13, 6, (uint32_t[]){(uint32_t)wh, (uint32_t)(wh >> 32), buf, 4, 1331, out}) == 1);
  CHECK(rd(buf) == 1 && (rd(out) & 0xff) == 0);
  CHECK(vcall(utils, 13, 6, (uint32_t[]){(uint32_t)wh, (uint32_t)(wh >> 32), buf, 4, 1331, out}) == 0);

  // anything else: a null object, never 0; the loaders the game must not load
  uint32_t unk = imp("steam_api.dll!SteamInternal_CreateInterface", 1, (uint32_t[]){gs("SteamNoSuch001")}, 0);
  CHECK(unk && vcall(unk, 7, 0, 0) == 0);
  CHECK(steam32_dll_blocked("uplay_r1_loader.dll") && steam32_dll_blocked("C:\\Game\\UPLAY_R1_LOADER") &&
        steam32_dll_blocked("./steamclient.dll") && !steam32_dll_blocked("bink2w32.dll"));
  imp("steam_api.dll!SteamAPI_Shutdown", 0, 0, 0);
  printf("test_steam32: OK\n");
  return 0;
}
