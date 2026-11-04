// The Mac stub's offline state behind a C interface (steam32_host.h).
//
// interfaces.cpp is compiled into this file (its classes and objects are file-local there); its
// shim_post_callback / shim_post_call_result queue (shim.cpp) is not used: the guest's callbacks
// carry Windows layouts and go through steam32.c. shim.cpp is linked for shim_config() and
// shim_log(); its exported SteamAPI_* functions, and with them its real backend, are never called.
#include <stdarg.h>

#include "interfaces.cpp"
#include "steam32_host.h"

extern "C" {

uint32_t steam32h_app_id(void) { return shim_config().app_id; }
uint64_t steam32h_steam_id(void) { return shim_config().steam_id; }
const char *steam32h_persona(void) { return shim_config().persona.c_str(); }
const char *steam32h_language(void) { return shim_config().language.c_str(); }
const char *steam32h_game_dir(void) { return shim_config().game_dir.c_str(); }
int steam32h_log_enabled(void) { return shim_config().log; }

void steam32h_log(const char *fmt, ...) {
    if (!shim_config().log) return;
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    shim_log("%s", b);
}

static void (*g_enumerate_post)(int, const void *, uint32_t);
static void enumerate_post() {
    uint32_t app = kEnumerateAppId;
    g_enumerate_post(kDlcInstalledCallback, &app, sizeof app);
}
void steam32h_enumerate(void (*post)(int id, const void *data, uint32_t size)) {
    const char *dir = getenv("LP_GAME_DIR");  // the game's folder is the app's own copy, not next to this binary
    g_enumerate_post = post;
    enumerate_press(dir && *dir ? dir : shim_config().game_dir, enumerate_post);
}

int steam32h_owned(uint32_t app) { return owned(app); }
int steam32h_subscribed(uint32_t app) { return subscribed(app); }
int steam32h_dlc_count(void) { return g_apps.GetDLCCount(); }
uint32_t steam32h_dlc_at(int i) {
    AppId_t app = 0;
    bool avail;
    return g_apps.BGetDLCDataByIndex(i, &app, &avail, nullptr, 0) ? app : 0;
}

int steam32h_user_data_folder(char *buf, int cub) { return g_user.GetUserDataFolder(buf, cub); }
uint32_t steam32h_seconds_since_computer_active(void) { return g_utils.GetSecondsSinceComputerActive(); }

void steam32h_stats_load(void) { g_stats.Load(); }
int steam32h_stats_save(void) { return g_stats.Save(); }
int steam32h_stat_get(const char *name, uint32_t *v) { return g_stats.GetStat(name, (int32_t *)v); }
int steam32h_stat_set(const char *name, uint32_t v) { return g_stats.SetStat(name, (int32_t)v); }
int steam32h_ach_get(const char *name, int *achieved, uint32_t *when) {
    bool a = false;
    bool ok = g_stats.GetAchievementAndUnlockTime(name, &a, when);
    if (achieved) *achieved = a;
    return ok;
}
int steam32h_ach_set(const char *name) { return g_stats.SetAchievement(name); }
int steam32h_ach_clear(const char *name) { return g_stats.ClearAchievement(name); }
int steam32h_stats_reset(int achievements_too) { return g_stats.ResetAllStats(achievements_too != 0); }

void steam32h_remote_init(void) { remote_dir(); }
int steam32h_file_write(const char *name, const void *data, int32_t cub) { return g_remote.FileWrite(name, data, cub); }
int32_t steam32h_file_read(const char *name, void *data, int32_t cub, uint32_t offset) {
    std::string p;
    if (!remote_path(name, &p) || !data) return 0;
    return read_into(p, data, cub, offset);
}
int steam32h_file_delete(const char *name) { return g_remote.FileDelete(name); }
int steam32h_file_exists(const char *name) { return g_remote.FileExists(name); }
int32_t steam32h_file_size(const char *name) { return g_remote.GetFileSize(name); }
int64_t steam32h_file_timestamp(const char *name) { return g_remote.GetFileTimestamp(name); }
int32_t steam32h_file_count(void) { return g_remote.GetFileCount(); }
const char *steam32h_file_name_and_size(int32_t i, int32_t *size) { return g_remote.GetFileNameAndSize(i, size); }
uint64_t steam32h_stream_open(const char *name) { return g_remote.FileWriteStreamOpen(name); }
int steam32h_stream_write(uint64_t h, const void *data, int32_t cub) { return g_remote.FileWriteStreamWriteChunk(h, data, cub); }
int steam32h_stream_close(uint64_t h) { return g_remote.FileWriteStreamClose(h); }
int steam32h_stream_cancel(uint64_t h) { return g_remote.FileWriteStreamCancel(h); }

}  // extern "C"
