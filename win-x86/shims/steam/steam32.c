// steam_api.dll for the 32-bit Windows guest, offline (steam32.h).
//
// ABI (MSVC, 32-bit):
//   * the flat exports are cdecl; bool results in al (eax set whole here), 64-bit ones in edx:eax;
//   * interface methods are thiscall: `this` in ecx, the callee removes its stack words; a
//     method returning CSteamID returns it through a hidden pointer, the first stack word;
//   * CCallbackBase: vtable [0] Run(void *, bool, SteamAPICall_t) (MSVC places overloaded
//     virtuals in reverse order of declaration), [1] Run(void *), [2] GetCallbackSizeBytes();
//     the flags byte at +4, the callback id at +8 (checked against the CCallResult objects the
//     game's steam_api.dll builds);
//   * callback structs are packed to 8 on Windows (VALVE_CALLBACK_PACK_LARGE), not 4 as on the
//     Mac.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "steam32.h"
#include "steam32_host.h"
#include "steam32_slots.h"
#include "win32_guestobj.h"
#include "win32_shims.h"

#define LOG(...) steam32h_log(__VA_ARGS__)

static uint32_t arg(rc_ctx *c, int k) { uint32_t v; memcpy(&v, RC32_H((uint32_t)(c->r[4] + 4 * k)), 4); return v; }
static uint64_t arg64(rc_ctx *c, int k) { return arg(c, k) | (uint64_t)arg(c, k + 1) << 32; }
static void ret(rc_ctx *c, uint32_t v) { c->r[0] = v; }
static void ret64(rc_ctx *c, uint64_t v) { c->r[0] = (uint32_t)v; c->r[2] = (uint32_t)(v >> 32); }
static const char *hstr(uint32_t g) { return g ? (const char *)RC32_H(g) : 0; }
static void put_bool(uint32_t g, int v) { if (g) *(uint8_t *)RC32_H(g) = v != 0; }
static void put32(uint32_t g, uint32_t v) { if (g) w32_wr32(g, v); }

static time_t start_time;

// --- callbacks and call results ------------------------------------------------------------
enum { CB_FLAG_REGISTERED = 1, CB_FLAG_GAMESERVER = 2 };
enum { CB_RUN_RESULT = 0, CB_RUN = 1, CB_SIZE = 2 };  // vtable slots (see the top)

typedef struct pending {
  int id;
  uint32_t size;
  uint64_t handle;  // call results
  uint8_t *data;
  struct pending *next;
} pending;

static pthread_mutex_t cb_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t *callbacks;  // registered CCallbackBase objects (guest)
static int ncallbacks, capcallbacks;
typedef struct { uint64_t h; uint32_t obj; } handler;
static handler *handlers;  // RegisterCallResult
static int nhandlers, caphandlers;
static pending *queue, **queue_tail = &queue;  // callbacks, in order
static pending *results;                       // finished calls, not yet taken
static uint64_t next_call = 1;
static uint32_t init_counter;

static pending *mkpending(int id, const void *data, uint32_t size) {
  pending *p = calloc(1, sizeof *p);
  p->id = id;
  p->size = size;
  p->data = malloc(size ? size : 1);
  memcpy(p->data, data, size);
  return p;
}

static void post_callback(int id, const void *data, uint32_t size) {
  pending *p = mkpending(id, data, size);
  pthread_mutex_lock(&cb_mu);
  *queue_tail = p;
  queue_tail = &p->next;
  pthread_mutex_unlock(&cb_mu);
}

// embed: the result struct starts with its own SteamAPICall_t.
static uint64_t post_call_result(int id, const void *data, uint32_t size, int embed) {
  pending *p = mkpending(id, data, size);
  pthread_mutex_lock(&cb_mu);
  p->handle = next_call++;
  if (embed) memcpy(p->data, &p->handle, 8);
  p->next = results;
  results = p;
  pthread_mutex_unlock(&cb_mu);
  return p->handle;
}

static pending **find_result(uint64_t h) {
  pending **pp = &results;
  while (*pp && (*pp)->handle != h) pp = &(*pp)->next;
  return pp;
}

static int is_registered(uint32_t obj) {
  for (int k = 0; k < ncallbacks; k++)
    if (callbacks[k] == obj) return 1;
  return 0;
}

static void run_one(rc_ctx *c, uint32_t obj, int slot, const pending *p, const uint32_t *extra, int nextra) {
  uint32_t vt = w32_rd32(obj), fn = w32_rd32(vt + 4 * slot);
  uint32_t size = p->size;
  uint32_t want = w32_guest_call(c, w32_rd32(vt + 4 * CB_SIZE), W32_THISCALL, obj, 0, 0, 0);
  if (want != size) LOG("callback %d: the listener expects %u bytes, the stub has %u", p->id, want, size);
  uint32_t buf = w32_galloc(size > want ? size : want);
  if (!buf) return;
  memcpy(RC32_H(buf), p->data, size);
  uint32_t a[4] = {buf};
  for (int k = 0; k < nextra; k++) a[1 + k] = extra[k];
  w32_guest_call(c, fn, W32_THISCALL, obj, a, 1 + nextra, 0);
  w32_gfree(buf, size > want ? size : want);
}

static void run_callbacks(rc_ctx *c) {
  pthread_mutex_lock(&cb_mu);
  pending *q = queue;
  queue = 0;
  queue_tail = &queue;
  pthread_mutex_unlock(&cb_mu);
  for (pending *p = q, *next; p; p = next) {
    next = p->next;
    // targets first, then each one if still registered (a listener may unregister another)
    pthread_mutex_lock(&cb_mu);
    int n = 0;
    uint32_t *t = malloc(sizeof *t * (ncallbacks ? ncallbacks : 1));
    for (int k = 0; k < ncallbacks; k++) {
      uint32_t o = callbacks[k];
      if ((int32_t)w32_rd32(o + 8) == p->id && !(*(uint8_t *)RC32_H(o + 4) & CB_FLAG_GAMESERVER)) t[n++] = o;
    }
    pthread_mutex_unlock(&cb_mu);
    LOG("dispatch callback %d to %d listener(s)", p->id, n);
    for (int k = 0; k < n; k++) {
      pthread_mutex_lock(&cb_mu);
      int live = is_registered(t[k]);
      pthread_mutex_unlock(&cb_mu);
      if (live) run_one(c, t[k], CB_RUN, p, 0, 0);
    }
    free(t);
    free(p->data);
    free(p);
  }
  // call results whose handler is registered
  for (;;) {
    pthread_mutex_lock(&cb_mu);
    pending *r = 0;
    uint32_t obj = 0;
    for (int k = 0; k < nhandlers && !r; k++) {
      pending **pp = find_result(handlers[k].h);
      if (*pp) {
        r = *pp;
        *pp = r->next;
        obj = handlers[k].obj;
        handlers[k] = handlers[--nhandlers];
      }
    }
    pthread_mutex_unlock(&cb_mu);
    if (!r) break;
    LOG("dispatch call result %llu (callback %d)", (unsigned long long)r->handle, r->id);
    uint32_t extra[3] = {0, (uint32_t)r->handle, (uint32_t)(r->handle >> 32)};  // bIOFailure, hSteamAPICall
    run_one(c, obj, CB_RUN_RESULT, r, extra, 3);
    free(r->data);
    free(r);
  }
}

// --- callback structs (packed to 8) --------------------------------------------------------
#pragma pack(push, 8)
typedef struct { uint64_t game_id; int32_t result; uint64_t steam_id; } UserStatsReceived_t;  // 1101
typedef struct { uint64_t game_id; int32_t result; } UserStatsStored_t;                       // 1102
typedef struct { int32_t result; } RemoteStorageFileWriteAsyncComplete_t;                     // 1331
typedef struct { uint64_t call; int32_t result; uint32_t offset, read; } RemoteStorageFileReadAsyncComplete_t;  // 1332
#pragma pack(pop)
_Static_assert(sizeof(UserStatsReceived_t) == 24, "pack 8");
_Static_assert(sizeof(UserStatsStored_t) == 16, "pack 8");
_Static_assert(sizeof(RemoteStorageFileReadAsyncComplete_t) == 24, "pack 8");
enum { k_EResultOK = 1, k_EResultFail = 2, k_EResultFileNotFound = 9 };

// --- interfaces ----------------------------------------------------------------------------
#define M(name) static uint64_t name(rc_ctx *c, uint32_t self, const uint32_t *a)
#define UNUSED (void)c, (void)self, (void)a

// ISteamClient
static uint32_t lookup(const char *version);
M(c_one) { UNUSED; return 1; }
M(c_CreateLocalUser) { UNUSED; put32(a[0], 1); return 1; }
M(c_GetI3) { UNUSED; return lookup(hstr(a[2])); }  // (user, pipe, version)
M(c_GetI2) { UNUSED; return lookup(hstr(a[1])); }  // (pipe, version)

// ISteamUser
M(u_GetSteamID) { UNUSED; w32_wr64(a[0], steam32h_steam_id()); return a[0]; }
M(u_GetUserDataFolder) {  // a host path: the file seam takes it from the guest from here on
  UNUSED;
  if (!a[0] || (int32_t)a[1] <= 0 || !steam32h_user_data_folder(RC32_H(a[0]), (int)a[1])) return 0;
  w32_host_root(RC32_H(a[0]));
  return 1;
}
M(u_GetAvailableVoice) { UNUSED; put32(a[0], 0); put32(a[1], 0); return 2; }  // k_EVoiceResultNotRecording
M(u_two) { UNUSED; return 2; }
M(u_GetAuthSessionTicket) { UNUSED; put32(a[2], 0); return 0; }  // k_HAuthTicketInvalid
M(u_UserHasLicenseForApp) {  // (CSteamID, AppId): HasLicense 0, DoesNotHaveLicense 1
  UNUSED;
  return w32_u64(a) == steam32h_steam_id() && a[2] == steam32h_app_id() ? 0 : 1;
}

// ISteamFriends: every `const char *` result is a string, never 0, except GetPlayerNickname
M(f_GetPersonaName) { UNUSED; return w32_gstr(steam32h_persona()); }
M(f_empty) { UNUSED; return w32_gstr(""); }
M(f_null) { UNUSED; return 0; }
M(f_GetFriendPersonaName) { UNUSED; return w32_gstr(w32_u64(a) == steam32h_steam_id() ? steam32h_persona() : ""); }

// ISteamUtils
M(t_GetSecondsSinceAppActive) { UNUSED; return (uint32_t)(time(0) - start_time); }
M(t_GetSecondsSinceComputerActive) { UNUSED; return steam32h_seconds_since_computer_active(); }
M(t_GetServerRealTime) { UNUSED; return (uint32_t)time(0); }
M(t_GetIPCountry) { UNUSED; return w32_gstr("US"); }
M(t_GetCurrentBatteryPower) { UNUSED; return 255; }  // on AC power
M(t_GetAppID) { UNUSED; return steam32h_app_id(); }
M(t_IsAPICallCompleted) {  // (h, bool *failed)
  UNUSED;
  pthread_mutex_lock(&cb_mu);
  int done = *find_result(w32_u64(a)) != 0;
  pthread_mutex_unlock(&cb_mu);
  put_bool(a[2], !done);
  return done;
}
M(t_GetAPICallFailureReason) {
  UNUSED;
  pthread_mutex_lock(&cb_mu);
  int done = *find_result(w32_u64(a)) != 0;
  pthread_mutex_unlock(&cb_mu);
  return done ? 0xffffffffu : 2;  // k_ESteamAPICallFailureNone / InvalidHandle
}
M(t_GetAPICallResult) {  // (h, void *out, int cub, int expected, bool *failed)
  UNUSED;
  pthread_mutex_lock(&cb_mu);
  pending **pp = find_result(w32_u64(a)), *r = *pp;
  int ok = r && r->id == (int32_t)a[4] && (int32_t)a[3] >= (int32_t)r->size && a[2];
  if (ok) *pp = r->next;
  pthread_mutex_unlock(&cb_mu);
  put_bool(a[5], !ok);
  if (!ok) return 0;
  memcpy(RC32_H(a[2]), r->data, r->size);
  free(r->data);
  free(r);
  return 1;
}
M(t_GetSteamUILanguage) { UNUSED; return w32_gstr(steam32h_language()); }
M(t_IsOverlayEnabled) {  // no overlay; the game asks before it opens the Shop (shims/steam/src/enumerate.h)
  UNUSED;
  steam32h_enumerate(post_callback);
  return 0;
}

// ISteamApps
M(p_GetLanguage) { UNUSED; return w32_gstr(steam32h_language()); }
M(p_BIsSubscribedApp) { UNUSED; return steam32h_subscribed(a[0]); }
M(p_BIsDlcInstalled) { UNUSED; return a[0] != steam32h_app_id() && steam32h_owned(a[0]); }
M(p_GetDLCCount) { UNUSED; return (uint32_t)steam32h_dlc_count(); }
M(p_BGetDLCDataByIndex) {  // (i, AppId_t *, bool *available, char *name, int cub)
  UNUSED;
  uint32_t app = steam32h_dlc_at((int32_t)a[0]);
  if (!app) return 0;
  put32(a[1], app);
  put_bool(a[2], 1);
  if (a[3] && (int32_t)a[4] > 0) *(char *)RC32_H(a[3]) = 0;  // names live in Steam's appinfo cache
  return 1;
}
M(p_GetAppInstallDir) {  // (app, char *buf, uint32 cub)
  UNUSED;
  static const char dir[] = "C:\\Rocksmith2014";  // the guest's path of the game (win32_kfile.c)
  size_t n = strlen(dir);
  if (a[0] != steam32h_app_id() || !a[1] || a[2] <= n) return 0;
  memcpy(RC32_H(a[1]), dir, n + 1);
  return (uint32_t)n;
}
M(p_BIsAppInstalled) { UNUSED; return a[0] == steam32h_app_id(); }
M(p_GetAppOwner) { UNUSED; w32_wr64(a[0], steam32h_steam_id()); return a[0]; }

// ISteamUserStats
M(s_RequestCurrentStats) {
  UNUSED;
  steam32h_stats_load();
  UserStatsReceived_t r = {steam32h_app_id(), k_EResultOK, steam32h_steam_id()};
  post_callback(1101, &r, sizeof r);
  return 1;
}
M(s_GetStat) {  // (name, int32 * or float *): the raw word either way
  UNUSED;
  uint32_t v = 0;
  if (!a[0] || !a[1] || !steam32h_stat_get(hstr(a[0]), &v)) return 0;
  w32_wr32(a[1], v);
  return 1;
}
M(s_SetStat) { UNUSED; return a[0] && steam32h_stat_set(hstr(a[0]), a[1]); }  // (name, int32 or float bits)
M(s_GetAchievement) {  // (name, bool *)
  UNUSED;
  int got = 0;
  if (!a[0] || !steam32h_ach_get(hstr(a[0]), &got, 0)) return 0;
  put_bool(a[1], got);
  return 1;
}
M(s_SetAchievement) { UNUSED; return a[0] && steam32h_ach_set(hstr(a[0])); }
M(s_ClearAchievement) { UNUSED; return a[0] && steam32h_ach_clear(hstr(a[0])); }
M(s_GetAchievementAndUnlockTime) {  // (name, bool *, uint32 *)
  UNUSED;
  int got = 0;
  uint32_t when = 0;
  if (!a[0] || !steam32h_ach_get(hstr(a[0]), &got, &when)) return 0;
  put_bool(a[1], got);
  put32(a[2], when);
  return 1;
}
M(s_StoreStats) {
  UNUSED;
  UserStatsStored_t r = {steam32h_app_id(), steam32h_stats_save() ? k_EResultOK : k_EResultFail};
  post_callback(1102, &r, sizeof r);
  return 1;
}
M(s_ResetAllStats) { UNUSED; return steam32h_stats_reset(a[0] & 0xff); }
M(s_minus1) { UNUSED; return 0xffffffffu; }

// ISteamRemoteStorage
M(r_FileWrite) {  // (name, data, int32 cub)
  UNUSED;
  if (!a[0] || (int32_t)a[2] < 0 || (!a[1] && a[2])) return 0;
  int ok = steam32h_file_write(hstr(a[0]), w32_h(a[1]), (int32_t)a[2]);
  LOG("FileWrite(%s, %d) -> %d", hstr(a[0]), (int32_t)a[2], ok);
  return ok;
}
M(r_FileRead) {  // (name, data, int32 cub)
  UNUSED;
  if (!a[0] || !a[1] || (int32_t)a[2] <= 0) return 0;
  int32_t n = steam32h_file_read(hstr(a[0]), RC32_H(a[1]), (int32_t)a[2], 0);
  LOG("FileRead(%s, %d) -> %d", hstr(a[0]), (int32_t)a[2], n);
  return (uint32_t)n;
}
M(r_FileWriteAsync) {  // (name, data, uint32 cub) -> SteamAPICall_t
  RemoteStorageFileWriteAsyncComplete_t r = {r_FileWrite(c, self, a) ? k_EResultOK : k_EResultFail};
  return post_call_result(1331, &r, sizeof r, 0);
}
// FileReadAsync keeps the bytes until FileReadAsyncComplete takes them
typedef struct async_read { uint64_t h; uint32_t n; uint8_t *data; struct async_read *next; } async_read;
static async_read *async_reads;
M(r_FileReadAsync) {  // (name, uint32 offset, uint32 cub) -> SteamAPICall_t
  UNUSED;
  RemoteStorageFileReadAsyncComplete_t r = {0, k_EResultFileNotFound, a[1], 0};
  uint8_t *buf = malloc(a[2] ? a[2] : 1);
  if (a[0] && steam32h_file_exists(hstr(a[0]))) {
    int32_t n = (int32_t)a[2] > 0 ? steam32h_file_read(hstr(a[0]), buf, (int32_t)a[2], a[1]) : 0;
    r.read = n > 0 ? (uint32_t)n : 0;
    r.result = k_EResultOK;
  }
  uint64_t h = post_call_result(1332, &r, sizeof r, 1);
  async_read *e = malloc(sizeof *e);
  pthread_mutex_lock(&cb_mu);
  *e = (async_read){h, r.read, buf, async_reads};
  async_reads = e;
  pthread_mutex_unlock(&cb_mu);
  return h;
}
M(r_FileReadAsyncComplete) {  // (h, void *out, uint32 cub)
  UNUSED;
  uint64_t h = w32_u64(a);
  pthread_mutex_lock(&cb_mu);
  async_read **pp = &async_reads, *e;
  while ((e = *pp) && e->h != h) pp = &e->next;
  if (e && a[2]) *pp = e->next;
  pthread_mutex_unlock(&cb_mu);
  if (!e || !a[2]) return 0;
  memcpy(RC32_H(a[2]), e->data, a[3] < e->n ? a[3] : e->n);
  free(e->data);
  free(e);
  return 1;
}
M(r_FileDelete) { UNUSED; return a[0] && steam32h_file_delete(hstr(a[0])); }
M(r_FileWriteStreamOpen) { UNUSED; return a[0] ? steam32h_stream_open(hstr(a[0])) : 0; }  // -> uint64 handle
M(r_FileWriteStreamWriteChunk) {  // (h, data, int32 cub)
  UNUSED;
  if ((int32_t)a[3] < 0 || (!a[2] && a[3])) return 0;
  return steam32h_stream_write(w32_u64(a), w32_h(a[2]), (int32_t)a[3]);
}
M(r_FileWriteStreamClose) { UNUSED; return steam32h_stream_close(w32_u64(a)); }
M(r_FileWriteStreamCancel) { UNUSED; return steam32h_stream_cancel(w32_u64(a)); }
M(r_FileExists) { UNUSED; return a[0] && steam32h_file_exists(hstr(a[0])); }
M(r_GetFileSize) { UNUSED; return a[0] ? (uint32_t)steam32h_file_size(hstr(a[0])) : 0; }
M(r_GetFileTimestamp) { UNUSED; return a[0] ? (uint64_t)steam32h_file_timestamp(hstr(a[0])) : 0; }
M(r_GetSyncPlatforms) { UNUSED; return 0xffffffffu; }  // k_ERemoteStoragePlatformAll
M(r_GetFileCount) { UNUSED; return (uint32_t)steam32h_file_count(); }
M(r_GetFileNameAndSize) {  // (int i, int32 *size)
  UNUSED;
  int32_t size = 0;
  const char *n = steam32h_file_name_and_size((int32_t)a[0], &size);
  put32(a[1], (uint32_t)size);
  return w32_gstr(n ? n : "");
}
M(r_GetQuota) { UNUSED; if (a[0]) w32_wr64(a[0], 1ull << 30); if (a[1]) w32_wr64(a[1], 1ull << 30); return 1; }

typedef struct { const char *name; w32_hostfn fn; } impl;
static const impl client_impl[] = {
    {"CreateSteamPipe", c_one}, {"BReleaseSteamPipe", c_one}, {"ConnectToGlobalUser", c_one},
    {"CreateLocalUser", c_CreateLocalUser}, {"GetISteamUser", c_GetI3}, {"GetISteamGameServer", c_GetI3},
    {"GetISteamFriends", c_GetI3}, {"GetISteamUtils", c_GetI2}, {"GetISteamMatchmaking", c_GetI3},
    {"GetISteamMatchmakingServers", c_GetI3}, {"GetISteamGenericInterface", c_GetI3},
    {"GetISteamUserStats", c_GetI3}, {"GetISteamGameServerStats", c_GetI3}, {"GetISteamApps", c_GetI3},
    {"GetISteamNetworking", c_GetI3}, {"GetISteamRemoteStorage", c_GetI3}, {"GetISteamScreenshots", c_GetI3},
    {"GetISteamGameSearch", c_GetI3}, {"BShutdownIfAllPipesClosed", c_one}, {"GetISteamHTTP", c_GetI3},
    {"DEPRECATED_GetISteamUnifiedMessages", c_GetI3}, {"GetISteamController", c_GetI3},
    {"GetISteamUGC", c_GetI3}, {"GetISteamAppList", c_GetI3}, {"GetISteamMusic", c_GetI3},
    {"GetISteamMusicRemote", c_GetI3}, {"GetISteamHTMLSurface", c_GetI3}, {"GetISteamInventory", c_GetI3},
    {"GetISteamVideo", c_GetI3}, {"GetISteamParentalSettings", c_GetI3}, {"GetISteamInput", c_GetI3},
    {"GetISteamParties", c_GetI3}, {"GetISteamRemotePlay", c_GetI3}, {0, 0}};
static const impl user_impl[] = {
    {"GetHSteamUser", c_one}, {"BLoggedOn", c_one}, {"GetSteamID", u_GetSteamID},
    {"GetUserDataFolder", u_GetUserDataFolder}, {"GetAvailableVoice", u_GetAvailableVoice},
    {"GetVoice", u_two}, {"DecompressVoice", u_two}, {"GetAuthSessionTicket", u_GetAuthSessionTicket},
    {"BeginAuthSession", c_one},  // k_EBeginAuthSessionResultInvalidTicket
    {"UserHasLicenseForApp", u_UserHasLicenseForApp}, {0, 0}};
static const impl friends_impl[] = {
    {"GetPersonaName", f_GetPersonaName}, {"GetPersonaState", c_one},  // k_EPersonaStateOnline
    {"GetFriendPersonaName", f_GetFriendPersonaName}, {"GetFriendPersonaNameHistory", f_empty},
    {"GetPlayerNickname", f_null}, {"GetFriendsGroupName", f_empty}, {"GetClanName", f_empty},
    {"GetClanTag", f_empty}, {"SetRichPresence", c_one}, {"GetFriendRichPresence", f_empty},
    {"GetFriendRichPresenceKeyByIndex", f_empty}, {0, 0}};
static const impl utils_impl[] = {
    {"GetSecondsSinceAppActive", t_GetSecondsSinceAppActive},
    {"GetSecondsSinceComputerActive", t_GetSecondsSinceComputerActive},
    {"GetConnectedUniverse", c_one},  // k_EUniversePublic
    {"GetServerRealTime", t_GetServerRealTime}, {"GetIPCountry", t_GetIPCountry},
    {"GetCurrentBatteryPower", t_GetCurrentBatteryPower}, {"GetAppID", t_GetAppID},
    {"IsAPICallCompleted", t_IsAPICallCompleted}, {"GetAPICallFailureReason", t_GetAPICallFailureReason},
    {"GetAPICallResult", t_GetAPICallResult}, {"GetSteamUILanguage", t_GetSteamUILanguage},
    {"IsOverlayEnabled", t_IsOverlayEnabled}, {0, 0}};
static const impl apps_impl[] = {
    {"BIsSubscribed", c_one}, {"GetCurrentGameLanguage", p_GetLanguage},
    {"GetAvailableGameLanguages", p_GetLanguage}, {"BIsSubscribedApp", p_BIsSubscribedApp},
    {"BIsDlcInstalled", p_BIsDlcInstalled}, {"GetDLCCount", p_GetDLCCount},
    {"BGetDLCDataByIndex", p_BGetDLCDataByIndex}, {"GetAppInstallDir", p_GetAppInstallDir},
    {"BIsAppInstalled", p_BIsAppInstalled}, {"GetAppOwner", p_GetAppOwner},
    {"GetLaunchQueryParam", f_empty}, {0, 0}};
static const impl stats_impl[] = {
    {"RequestCurrentStats", s_RequestCurrentStats}, {"GetStat", s_GetStat}, {"GetStat0", s_GetStat},
    {"SetStat", s_SetStat}, {"SetStat0", s_SetStat}, {"UpdateAvgRateStat", c_one},
    {"GetAchievement", s_GetAchievement}, {"SetAchievement", s_SetAchievement},
    {"ClearAchievement", s_ClearAchievement}, {"GetAchievementAndUnlockTime", s_GetAchievementAndUnlockTime},
    {"StoreStats", s_StoreStats}, {"GetAchievementDisplayAttribute", f_empty},
    {"IndicateAchievementProgress", c_one}, {"GetAchievementName", f_empty},
    {"ResetAllStats", s_ResetAllStats}, {"GetLeaderboardName", f_empty},
    {"GetMostAchievedAchievementInfo", s_minus1}, {"GetNextMostAchievedAchievementInfo", s_minus1}, {0, 0}};
static const impl remote_impl[] = {
    {"FileWrite", r_FileWrite}, {"FileRead", r_FileRead}, {"FileWriteAsync", r_FileWriteAsync},
    {"FileReadAsync", r_FileReadAsync}, {"FileReadAsyncComplete", r_FileReadAsyncComplete},
    {"FileForget", c_one}, {"FileDelete", r_FileDelete}, {"SetSyncPlatforms", c_one},
    {"FileWriteStreamOpen", r_FileWriteStreamOpen}, {"FileWriteStreamWriteChunk", r_FileWriteStreamWriteChunk},
    {"FileWriteStreamClose", r_FileWriteStreamClose}, {"FileWriteStreamCancel", r_FileWriteStreamCancel},
    {"FileExists", r_FileExists}, {"FilePersisted", r_FileExists}, {"GetFileSize", r_GetFileSize},
    {"GetFileTimestamp", r_GetFileTimestamp}, {"GetSyncPlatforms", r_GetSyncPlatforms},
    {"GetFileCount", r_GetFileCount}, {"GetFileNameAndSize", r_GetFileNameAndSize}, {"GetQuota", r_GetQuota},
    {"IsCloudEnabledForAccount", c_one}, {"IsCloudEnabledForApp", c_one}, {0, 0}};

// Interface versions: the prefix picks the layout; a version other than the one the layout was
// read for is logged (its slots may differ).
typedef struct {
  const char *prefix, *expected;
  const steam32_slotdef *defs;
  int n;
  const impl *impl;
  w32_slot *slots;  // built on first use
  uint32_t obj;
} iface;
#define DEFS(x) steam32_##x, (int)(sizeof steam32_##x / sizeof steam32_##x[0])
static iface ifaces[] = {
    {"SteamClient", "SteamClient019", DEFS(ISteamClient), client_impl},
    {"SteamUser0", "SteamUser020", DEFS(ISteamUser), user_impl},  // not STEAMUSERSTATS_...
    {"SteamFriends", "SteamFriends017", DEFS(ISteamFriends), friends_impl},
    {"SteamUtils", "SteamUtils009", DEFS(ISteamUtils), utils_impl},
    {"STEAMAPPS_INTERFACE_VERSION", "STEAMAPPS_INTERFACE_VERSION008", DEFS(ISteamApps), apps_impl},
    {"STEAMUSERSTATS_INTERFACE_VERSION", "STEAMUSERSTATS_INTERFACE_VERSION011", DEFS(ISteamUserStats), stats_impl},
    {"STEAMREMOTESTORAGE_INTERFACE_VERSION", "STEAMREMOTESTORAGE_INTERFACE_VERSION014", DEFS(ISteamRemoteStorage), remote_impl},
    // the rest: every slot returns 0 / false / an invalid handle, with the right stack words
    {"STEAMHTTP_INTERFACE_VERSION", "STEAMHTTP_INTERFACE_VERSION003", DEFS(ISteamHTTP), 0},
    {"SteamMatchMakingServers", "SteamMatchMakingServers002", DEFS(ISteamMatchmakingServers), 0},
    {"SteamMatchMaking", "SteamMatchMaking009", DEFS(ISteamMatchmaking), 0},
    {"SteamNetworking", "SteamNetworking005", DEFS(ISteamNetworking), 0},
    {"STEAMSCREENSHOTS_INTERFACE_VERSION", "STEAMSCREENSHOTS_INTERFACE_VERSION003", DEFS(ISteamScreenshots), 0},
    {"STEAMUGC_INTERFACE_VERSION", "STEAMUGC_INTERFACE_VERSION012", DEFS(ISteamUGC), 0},
    {"SteamController", "SteamController007", DEFS(ISteamController), 0},
    {"SteamInput", "SteamInput001", DEFS(ISteamInput), 0},
    {"STEAMAPPLIST_INTERFACE_VERSION", "STEAMAPPLIST_INTERFACE_VERSION001", DEFS(ISteamAppList), 0},
    {"STEAMMUSIC_INTERFACE_VERSION", "STEAMMUSIC_INTERFACE_VERSION001", DEFS(ISteamMusic), 0},
    {"STEAMMUSICREMOTE_INTERFACE_VERSION", "STEAMMUSICREMOTE_INTERFACE_VERSION001", DEFS(ISteamMusicRemote), 0},
    {"STEAMHTMLSURFACE_INTERFACE_VERSION", "STEAMHTMLSURFACE_INTERFACE_VERSION_005", DEFS(ISteamHTMLSurface), 0},
    {"STEAMINVENTORY_INTERFACE_V", "STEAMINVENTORY_INTERFACE_V003", DEFS(ISteamInventory), 0},
    {"STEAMVIDEO_INTERFACE_V", "STEAMVIDEO_INTERFACE_V002", DEFS(ISteamVideo), 0},
    {"STEAMPARENTALSETTINGS_INTERFACE_VERSION", "STEAMPARENTALSETTINGS_INTERFACE_VERSION001", DEFS(ISteamParentalSettings), 0},
    {"SteamParties", "SteamParties002", DEFS(ISteamParties), 0},
    {"STEAMREMOTEPLAY_INTERFACE_VERSION", "STEAMREMOTEPLAY_INTERFACE_VERSION001", DEFS(ISteamRemotePlay), 0},
    {"SteamMatchGameSearch", "SteamMatchGameSearch001", DEFS(ISteamGameSearch), 0},
};
static pthread_mutex_t if_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t null_obj;

static uint32_t iface_obj(iface *f) {
  if (f->obj) return f->obj;
  w32_slot *s = calloc((size_t)f->n, sizeof *s);
  for (int k = 0; k < f->n; k++) {
    s[k] = (w32_slot){f->defs[k].name, f->defs[k].words, f->defs[k].sret, 0};
    for (const impl *m = f->impl; m && m->name; m++)
      if (!strcmp(m->name, f->defs[k].name)) s[k].fn = m->fn;
  }
  f->slots = s;
  f->obj = w32_object(w32_vtable(s, f->n, W32_THISCALL, 0), 16);
  return f->obj;
}

// An interface nothing here models: 160 slots that return 0 and remove nothing (the stack words
// are unknown), so at least a stray call does not hit a null pointer.
static uint32_t null_iface(void) {
  if (!null_obj) {
    static w32_slot s[160];
    for (int k = 0; k < 160; k++) s[k] = (w32_slot){"steam: unknown interface", 0, 0, 0};
    null_obj = w32_object(w32_vtable(s, 160, W32_THISCALL, 0), 16);
  }
  return null_obj;
}

static uint32_t lookup(const char *v) {
  pthread_mutex_lock(&if_mu);
  uint32_t o = 0;
  for (size_t k = 0; v && k < sizeof ifaces / sizeof ifaces[0] && !o; k++) {
    iface *f = &ifaces[k];
    if (strncmp(v, f->prefix, strlen(f->prefix))) continue;
    if (strcmp(v, f->expected)) LOG("WARNING: %s requested, the layout is %s's; slots may differ", v, f->expected);
    o = iface_obj(f);
  }
  if (!o) {
    LOG("unknown interface %s: null object", v ? v : "(null)");
    o = null_iface();
  }
  pthread_mutex_unlock(&if_mu);
  return o;
}

uint32_t steam32_interface(const char *version) { return lookup(version); }

// --- exports (cdecl: the arguments stay on the stack) ---------------------------------------
#define X(name) static void x_##name(rc_ctx *c)

X(SteamAPI_Init) {
  start_time = time(0);
  pthread_mutex_lock(&cb_mu);
  init_counter++;
  pthread_mutex_unlock(&cb_mu);
  steam32h_stats_load();
  steam32h_remote_init();
  LOG("stub SteamAPI_Init (win32): app %u, user %llu (%s), language %s, DLC %d", steam32h_app_id(),
      (unsigned long long)steam32h_steam_id(), steam32h_persona(), steam32h_language(), steam32h_dlc_count());
  ret(c, 1);
}
X(SteamAPI_Shutdown) { steam32h_stats_save(); ret(c, 0); }
X(SteamAPI_RunCallbacks) { run_callbacks(c); ret(c, 0); }
X(Steam_RunCallbacks) {  // (pipe, bGameServer)
  if (!(arg(c, 1) & 0xff)) run_callbacks(c);
  ret(c, 0);
}
X(SteamAPI_RestartAppIfNecessary) { LOG("SteamAPI_RestartAppIfNecessary(%u) -> false", arg(c, 0)); ret(c, 0); }
X(one) { ret(c, 1); }
X(zero) { ret(c, 0); }
X(zero64) { ret64(c, 0); }
X(SteamAPI_RegisterCallback) {  // (CCallbackBase *, int id)
  uint32_t o = arg(c, 0);
  int id = (int32_t)arg(c, 1);
  pthread_mutex_lock(&cb_mu);
  *(uint8_t *)RC32_H(o + 4) |= CB_FLAG_REGISTERED;
  w32_wr32(o + 8, (uint32_t)id);
  if (!is_registered(o)) {
    if (ncallbacks == capcallbacks) {
      capcallbacks = capcallbacks ? 2 * capcallbacks : 64;
      callbacks = realloc(callbacks, sizeof *callbacks * (size_t)capcallbacks);
    }
    callbacks[ncallbacks++] = o;
  }
  pthread_mutex_unlock(&cb_mu);
  LOG("RegisterCallback(0x%x, %d)", o, id);
  ret(c, 0);
}
X(SteamAPI_UnregisterCallback) {
  uint32_t o = arg(c, 0);
  pthread_mutex_lock(&cb_mu);
  *(uint8_t *)RC32_H(o + 4) &= ~CB_FLAG_REGISTERED;
  for (int k = 0; k < ncallbacks; k++)
    if (callbacks[k] == o) {
      memmove(callbacks + k, callbacks + k + 1, sizeof *callbacks * (size_t)(ncallbacks - k - 1));
      ncallbacks--;
      break;
    }
  pthread_mutex_unlock(&cb_mu);
  ret(c, 0);
}
X(SteamAPI_RegisterCallResult) {  // (CCallbackBase *, SteamAPICall_t)
  pthread_mutex_lock(&cb_mu);
  if (nhandlers == caphandlers) {
    caphandlers = caphandlers ? 2 * caphandlers : 64;
    handlers = realloc(handlers, sizeof *handlers * (size_t)caphandlers);
  }
  handlers[nhandlers++] = (handler){arg64(c, 1), arg(c, 0)};
  pthread_mutex_unlock(&cb_mu);
  ret(c, 0);
}
X(SteamAPI_UnregisterCallResult) {
  uint32_t o = arg(c, 0);
  uint64_t h = arg64(c, 1);
  pthread_mutex_lock(&cb_mu);
  for (int k = 0; k < nhandlers; k++)
    if (handlers[k].obj == o && handlers[k].h == h) {
      handlers[k] = handlers[--nhandlers];
      break;
    }
  pthread_mutex_unlock(&cb_mu);
  ret(c, 0);
}
// The SDK's inline accessors pass a static block { void (*pFn)(void *ctx); uintptr_t counter;
// <context> } and want the context back, refilled by pFn whenever SteamAPI_Init ran since.
X(SteamInternal_ContextInit) {
  uint32_t d = arg(c, 0);
  pthread_mutex_lock(&cb_mu);
  uint32_t want = init_counter;
  pthread_mutex_unlock(&cb_mu);
  if (w32_rd32(d + 4) != want) {
    uint32_t ctx = d + 8;
    w32_guest_call(c, w32_rd32(d), W32_CDECL, 0, &ctx, 1, 0);  // the guest's own code: unlocked
    w32_wr32(d + 4, want);
  }
  ret(c, d + 8);
}
X(SteamInternal_CreateInterface) { ret(c, lookup(hstr(arg(c, 0)))); }
X(SteamInternal_FindOrCreateUserInterface) { ret(c, lookup(hstr(arg(c, 1)))); }  // (user, version)
X(SteamClient) { ret(c, lookup("SteamClient019")); }
// pre-1.43 accessors (not exported by the game's DLL; served in case another build imports them)
X(SteamUser) { ret(c, lookup("SteamUser020")); }
X(SteamFriends) { ret(c, lookup("SteamFriends017")); }
X(SteamUtils) { ret(c, lookup("SteamUtils009")); }
X(SteamApps) { ret(c, lookup("STEAMAPPS_INTERFACE_VERSION008")); }
X(SteamUserStats) { ret(c, lookup("STEAMUSERSTATS_INTERFACE_VERSION011")); }
X(SteamRemoteStorage) { ret(c, lookup("STEAMREMOTESTORAGE_INTERFACE_VERSION014")); }
X(SteamMatchmaking) { ret(c, lookup("SteamMatchMaking009")); }
X(SteamHTTP) { ret(c, lookup("STEAMHTTP_INTERFACE_VERSION003")); }
X(SteamAPI_GetSteamInstallPath) { ret(c, w32_gstr("")); }

static const struct { const char *name; rc_fn fn; } exports[] = {
    {"SteamAPI_Init", x_SteamAPI_Init},
    {"SteamAPI_InitSafe", x_SteamAPI_Init},
    {"SteamAPI_Shutdown", x_SteamAPI_Shutdown},
    {"SteamAPI_RunCallbacks", x_SteamAPI_RunCallbacks},
    {"Steam_RunCallbacks", x_Steam_RunCallbacks},
    {"SteamAPI_RestartAppIfNecessary", x_SteamAPI_RestartAppIfNecessary},
    {"SteamAPI_IsSteamRunning", x_one},
    {"SteamAPI_GetHSteamUser", x_one},
    {"SteamAPI_GetHSteamPipe", x_one},
    {"GetHSteamUser", x_one},
    {"GetHSteamPipe", x_one},
    {"Steam_GetHSteamUserCurrent", x_one},
    {"SteamAPI_RegisterCallback", x_SteamAPI_RegisterCallback},
    {"SteamAPI_UnregisterCallback", x_SteamAPI_UnregisterCallback},
    {"SteamAPI_RegisterCallResult", x_SteamAPI_RegisterCallResult},
    {"SteamAPI_UnregisterCallResult", x_SteamAPI_UnregisterCallResult},
    {"SteamInternal_ContextInit", x_SteamInternal_ContextInit},
    {"SteamInternal_CreateInterface", x_SteamInternal_CreateInterface},
    {"SteamInternal_FindOrCreateUserInterface", x_SteamInternal_FindOrCreateUserInterface},
    {"SteamInternal_FindOrCreateGameServerInterface", x_SteamInternal_FindOrCreateUserInterface},
    {"SteamClient", x_SteamClient},
    {"SteamUser", x_SteamUser},
    {"SteamFriends", x_SteamFriends},
    {"SteamUtils", x_SteamUtils},
    {"SteamApps", x_SteamApps},
    {"SteamUserStats", x_SteamUserStats},
    {"SteamRemoteStorage", x_SteamRemoteStorage},
    {"SteamMatchmaking", x_SteamMatchmaking},
    {"SteamHTTP", x_SteamHTTP},
    {"SteamAPI_GetSteamInstallPath", x_SteamAPI_GetSteamInstallPath},
    // no-ops: crash reporting (Windows only), thread memory, interface registration
    {"SteamAPI_ReleaseCurrentThreadMemory", x_zero},
    {"SteamAPI_SetMiniDumpComment", x_zero},
    {"SteamAPI_WriteMiniDump", x_zero},
    {"SteamAPI_SetBreakpadAppID", x_zero},
    {"SteamAPI_UseBreakpadCrashHandler", x_zero},
    {"SteamAPI_SetTryCatchCallbacks", x_zero},
    {"Steam_RegisterInterfaceFuncs", x_zero},
    // no game server, no anonymous user
    {"SteamAPI_InitAnonymousUser", x_zero},
    {"SteamInternal_GameServer_Init", x_zero},
    {"SteamGameServer_InitSafe", x_zero},
    {"SteamGameServer_Shutdown", x_zero},
    {"SteamGameServer_RunCallbacks", x_zero},
    {"SteamGameServer_BSecure", x_zero},
    {"SteamGameServer_GetHSteamPipe", x_zero},
    {"SteamGameServer_GetHSteamUser", x_zero},
    {"SteamGameServer_GetIPCCallCount", x_zero},
    {"SteamGameServer_GetSteamID", x_zero64},
};

rc_fn steam32_import(const char *import_name) {
  const char *bang = strchr(import_name, '!');
  if (!bang || bang - import_name != 13 || strncasecmp(import_name, "steam_api.dll", 13)) return 0;
  for (size_t k = 0; k < sizeof exports / sizeof exports[0]; k++)
    if (!strcmp(exports[k].name, bang + 1)) return exports[k].fn;
  return 0;
}

int steam32_dll_blocked(const char *dll) {
  static const char *const blocked[] = {
      "uplay_r1_loader.dll", "uplay_r164_loader.dll", "uplay_r2_loader.dll", "upc_r1_loader.dll",
      "upc_r2_loader.dll", "uplay_r1.dll", "uplay_ach.dll", "steamclient.dll", "steamclient64.dll",
      "gameoverlayrenderer.dll",
  };
  if (!dll) return 0;
  const char *base = dll;
  for (const char *p = dll; *p; p++)
    if (*p == '\\' || *p == '/') base = p + 1;
  for (size_t k = 0; k < sizeof blocked / sizeof blocked[0]; k++) {
    size_t n = strlen(blocked[k]);
    // with or without the .dll the caller may leave out
    if (!strcasecmp(base, blocked[k]) || (strlen(base) == n - 4 && !strncasecmp(base, blocked[k], n - 4)))
      return 1;
  }
  return 0;
}
