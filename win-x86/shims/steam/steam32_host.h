// The host side of the win-x86 Steam stub: the Mac stub's offline state (shims/steam/src), as
// plain C over host pointers. Identity, language, owned DLC, the Steam Cloud directory and the
// local stats come from there unchanged (steam_shim.ini / RS_STEAM_* settings, the same data
// directory), so both builds behave alike. Nothing here talks to Steam: only the Mac stub's
// stub-mode objects are used, never its real backend.
#ifndef STEAM32_HOST_H
#define STEAM32_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// identity
uint32_t steam32h_app_id(void);
uint64_t steam32h_steam_id(void);
const char *steam32h_persona(void);
const char *steam32h_language(void);
const char *steam32h_game_dir(void);
int steam32h_log_enabled(void);
void steam32h_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// apps: the base app and the configured DLC (all of them with RS_STEAM_DLC=all)
int steam32h_owned(uint32_t app);
int steam32h_dlc_count(void);
uint32_t steam32h_dlc_at(int i);  // 0 when out of range

// the ENUMERATE menu entry (shims/steam/src/enumerate.h): POST gets the callback, on another thread
void steam32h_enumerate(void (*post)(int id, const void *data, uint32_t size));

// user
int steam32h_user_data_folder(char *buf, int cub);
uint32_t steam32h_seconds_since_computer_active(void);

// stats: values are raw 32-bit words (int32 or float bits)
void steam32h_stats_load(void);
int steam32h_stats_save(void);
int steam32h_stat_get(const char *name, uint32_t *v);
int steam32h_stat_set(const char *name, uint32_t v);
int steam32h_ach_get(const char *name, int *achieved, uint32_t *when);
int steam32h_ach_set(const char *name);
int steam32h_ach_clear(const char *name);
int steam32h_stats_reset(int achievements_too);

// Steam Cloud (<data dir>/<account>/<app>/remote, seeded from Steam's copy, without `crd`)
void steam32h_remote_init(void);
int steam32h_file_write(const char *name, const void *data, int32_t cub);
int32_t steam32h_file_read(const char *name, void *data, int32_t cub, uint32_t offset);
int steam32h_file_delete(const char *name);
int steam32h_file_exists(const char *name);
int32_t steam32h_file_size(const char *name);
int64_t steam32h_file_timestamp(const char *name);
int32_t steam32h_file_count(void);
const char *steam32h_file_name_and_size(int32_t i, int32_t *size);
uint64_t steam32h_stream_open(const char *name);
int steam32h_stream_write(uint64_t h, const void *data, int32_t cub);
int steam32h_stream_close(uint64_t h);
int steam32h_stream_cancel(uint64_t h);

#ifdef __cplusplus
}
#endif
#endif
