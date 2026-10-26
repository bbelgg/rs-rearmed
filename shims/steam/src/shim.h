// Shared declarations for the libsteam_api replacement.
//
// Types mirror Steamworks SDK 1.4x (the SDK Rocksmith 2014 was linked
// against: SteamClient019, SteamUser020, SteamFriends017, SteamUtils009,
// STEAMAPPS_INTERFACE_VERSION008, STEAMUSERSTATS_INTERFACE_VERSION011,
// STEAMREMOTESTORAGE_INTERFACE_VERSION014, STEAMHTTP_INTERFACE_VERSION003).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <set>
#include <string>

typedef int32_t HSteamUser;
typedef int32_t HSteamPipe;
typedef uint32_t AppId_t;
typedef uint64_t SteamAPICall_t;
typedef uint64_t CSteamID;  // trivially copyable 8-byte class in the SDK; returned in rax/x0

#define S_EXPORT extern "C" __attribute__((visibility("default")))

// Steam packs callback structs to 4 bytes on macOS/Linux (VALVE_CALLBACK_PACK_SMALL).
#pragma pack(push, 4)
struct UserStatsReceived_t {  // k_iSteamUserStatsCallbacks + 1
    enum { k_iCallback = 1101 };
    uint64_t m_nGameID;
    int32_t m_eResult;
    CSteamID m_steamIDUser;
};
struct UserStatsStored_t {  // k_iSteamUserStatsCallbacks + 2
    enum { k_iCallback = 1102 };
    uint64_t m_nGameID;
    int32_t m_eResult;
};
struct RemoteStorageFileWriteAsyncComplete_t {  // k_iClientRemoteStorageCallbacks + 31
    enum { k_iCallback = 1331 };
    int32_t m_eResult;
};
struct RemoteStorageFileReadAsyncComplete_t {  // k_iClientRemoteStorageCallbacks + 32
    enum { k_iCallback = 1332 };
    SteamAPICall_t m_hFileReadAsync;
    int32_t m_eResult;
    uint32_t m_nOffset;
    uint32_t m_cubRead;
};
#pragma pack(pop)

enum { k_EResultOK = 1, k_EResultFail = 2, k_EResultFileNotFound = 9 };

struct ShimConfig {
    std::string mode;          // auto | real | stub
    std::string real_lib;      // path to Valve's (arm64-capable) libsteam_api
    AppId_t app_id = 221680;
    CSteamID steam_id = 0;
    std::string persona;
    std::string language;
    std::string game_dir;      // .../steamapps/common/Rocksmith2014
    std::string data_dir;      // stub-mode userdata root: <data_dir>/<accountid>/<appid>/
    std::string steam_root;    // ~/Library/Application Support/Steam
    std::set<AppId_t> dlc;     // stub mode: DLC reported as owned (installed DLC by default)
    bool dlc_all = false;      // stub mode: every app id is owned (RS_STEAM_DLC=all)
    bool log = false;
};

const ShimConfig &shim_config();
void shim_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Callback / call-result dispatch (stub mode).
void shim_post_callback(int id, const void *data, size_t size);
// embed_handle: the result struct starts with its own SteamAPICall_t (e.g. FileReadAsync).
SteamAPICall_t shim_post_call_result(int id, const void *data, size_t size, bool embed_handle = false);
bool shim_call_completed(SteamAPICall_t h, bool *failed);
bool shim_call_result(SteamAPICall_t h, void *out, int cub, int expected_id, bool *failed);

// Stub-mode interface lookup by version string ("SteamUser020", ...).
void *shim_find_interface(const char *version);
void shim_stub_init();
void shim_stub_shutdown();
