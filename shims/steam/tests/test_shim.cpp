// Exercises libsteam_api.dylib the way an SDK 1.4x game does: inline
// context accessors, CCallback/CCallResult objects and raw vtable calls.
// Built for arm64 and x86_64; run with RS_STEAM_MODE=stub.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t HSteamUser;
typedef uint64_t SteamAPICall_t;

extern "C" {
bool SteamAPI_RestartAppIfNecessary(uint32_t);
bool SteamAPI_Init();
void SteamAPI_Shutdown();
void SteamAPI_RunCallbacks();
HSteamUser SteamAPI_GetHSteamUser();
void SteamAPI_RegisterCallback(void *, int);
void SteamAPI_UnregisterCallback(void *);
void SteamAPI_RegisterCallResult(void *, SteamAPICall_t);
void SteamAPI_UnregisterCallResult(void *, SteamAPICall_t);
void *SteamInternal_ContextInit(void *);
void *SteamInternal_CreateInterface(const char *);
void *SteamInternal_FindOrCreateUserInterface(HSteamUser, const char *);
}

static int g_failures = 0;
#define EXPECT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                              \
        }                                                              \
    } while (0)

// --- SDK-style accessors (STEAM_DEFINE_USER_INTERFACE_ACCESSOR) -------------
#define ACCESSOR(name, version)                                                   \
    static void Init_##name(void **p) {                                           \
        *p = SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), version); \
    }                                                                             \
    static void *name() {                                                         \
        static void *s_ctx[3] = {(void *)&Init_##name, 0, 0};                     \
        return *(void **)SteamInternal_ContextInit(s_ctx);                        \
    }
ACCESSOR(SteamUser, "SteamUser020")
ACCESSOR(SteamFriends, "SteamFriends017")
ACCESSOR(SteamUtils, "SteamUtils009")
ACCESSOR(SteamApps, "STEAMAPPS_INTERFACE_VERSION008")
ACCESSOR(SteamUserStats, "STEAMUSERSTATS_INTERFACE_VERSION011")
ACCESSOR(SteamRemoteStorage, "STEAMREMOTESTORAGE_INTERFACE_VERSION014")
ACCESSOR(SteamHTTP, "STEAMHTTP_INTERFACE_VERSION003")

// Call vtable slot `idx` of interface `obj` with signature R(Args...).
template <typename R, typename... A>
static R vcall(void *obj, int idx, A... args) {
    typedef R (*Fn)(void *, A...);
    return ((Fn)(*(void ***)obj)[idx])(obj, args...);
}

// --- CCallbackBase-compatible listener --------------------------------------
#pragma pack(push, 4)
struct UserStatsReceived_t {
    uint64_t m_nGameID;
    int32_t m_eResult;
    uint64_t m_steamIDUser;
};
struct FileReadAsyncComplete_t {
    uint64_t m_hFileReadAsync;
    int32_t m_eResult;
    uint32_t m_nOffset;
    uint32_t m_cubRead;
};
#pragma pack(pop)

class CCallbackBase {
public:
    virtual void Run(void *p) = 0;
    virtual void Run(void *p, bool io_failure, SteamAPICall_t h) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    uint8_t m_nCallbackFlags = 0;
    int m_iCallback = 0;
};

struct StatsListener : CCallbackBase {
    int hits = 0;
    UserStatsReceived_t last = {};
    void Run(void *p) override { hits++, memcpy(&last, p, sizeof last); }
    void Run(void *p, bool, SteamAPICall_t) override { Run(p); }
    int GetCallbackSizeBytes() override { return sizeof(UserStatsReceived_t); }
};

struct ReadResult : CCallbackBase {
    int hits = 0;
    SteamAPICall_t handle = 0;
    FileReadAsyncComplete_t last = {};
    void Run(void *) override {}
    void Run(void *p, bool io_failure, SteamAPICall_t h) override {
        hits++, handle = h, memcpy(&last, p, sizeof last);
        EXPECT(!io_failure);
    }
    int GetCallbackSizeBytes() override { return sizeof(FileReadAsyncComplete_t); }
};

int main() {
    EXPECT(!SteamAPI_RestartAppIfNecessary(221680));

    // Before Init the accessors must hand back NULL, not garbage.
    EXPECT(SteamUser() == nullptr);
    EXPECT(SteamAPI_Init());
    void *user = SteamUser(), *friends = SteamFriends(), *utils = SteamUtils(), *apps = SteamApps();
    void *stats = SteamUserStats(), *remote = SteamRemoteStorage(), *http = SteamHTTP();
    EXPECT(user && friends && utils && apps && stats && remote && http);
    EXPECT(SteamUser() == user);  // cached after first fill
    EXPECT(SteamInternal_CreateInterface("SteamClient019") != nullptr);

    // ISteamUser
    EXPECT(vcall<bool>(user, 1));                                  // BLoggedOn
    uint64_t sid = vcall<uint64_t>(user, 2);                       // GetSteamID
    EXPECT((sid >> 52) == 0x011);                                  // public individual account
    char folder[1024] = {0};
    EXPECT(vcall<bool>(user, 6, folder, (int)sizeof folder) && strstr(folder, "/221680/local"));

    // ISteamFriends / ISteamUtils / ISteamApps
    const char *persona = vcall<const char *>(friends, 0);
    EXPECT(persona && *persona);
    EXPECT(vcall<uint32_t>(utils, 9) == 221680);                   // GetAppID
    EXPECT(vcall<const char *>(utils, 23) != nullptr);             // GetSteamUILanguage
    EXPECT(vcall<bool>(apps, 0));                                  // BIsSubscribed
    EXPECT(vcall<bool>(apps, 6, (uint32_t)221680));                // BIsSubscribedApp
    EXPECT(!vcall<bool>(apps, 7, (uint32_t)222000));               // BIsDlcInstalled (DLC not faked)
    EXPECT(strlen(vcall<const char *>(apps, 4)) > 0);              // GetCurrentGameLanguage
    EXPECT(vcall<uint64_t>(apps, 20) == sid);                      // GetAppOwner
    EXPECT(vcall<uint32_t>(http, 0, 1, "http://x") == 0);          // CreateHTTPRequest -> invalid

    // ISteamUserStats: RequestCurrentStats -> UserStatsReceived_t via RunCallbacks
    StatsListener listener;
    SteamAPI_RegisterCallback(&listener, 1101);
    EXPECT(listener.m_nCallbackFlags & 1);
    EXPECT(vcall<bool>(stats, 0));
    SteamAPI_RunCallbacks();
    EXPECT(listener.hits == 1 && listener.last.m_eResult == 1 && listener.last.m_nGameID == 221680 &&
           listener.last.m_steamIDUser == sid);
    EXPECT(vcall<bool>(stats, 4, "TestFloat", 2.5f));              // SetStat(float) uses s0/xmm0
    float f = 0;
    EXPECT(vcall<bool>(stats, 2, "TestFloat", &f) && f == 2.5f);
    SteamAPI_UnregisterCallback(&listener);
    EXPECT(!(listener.m_nCallbackFlags & 1));
    vcall<bool>(stats, 0);
    SteamAPI_RunCallbacks();
    EXPECT(listener.hits == 1);

    // ISteamRemoteStorage round trip, sync and async
    const char msg[] = "hello cloud";
    EXPECT(vcall<bool>(remote, 0, "shim_test.bin", (const void *)msg, (int32_t)sizeof msg));
    EXPECT(vcall<bool>(remote, 13, "shim_test.bin"));              // FileExists
    EXPECT(vcall<int32_t>(remote, 15, "shim_test.bin") == (int32_t)sizeof msg);
    char buf[64] = {0};
    EXPECT(vcall<int32_t>(remote, 1, "shim_test.bin", (void *)buf, (int32_t)sizeof buf) == (int32_t)sizeof msg);
    EXPECT(strcmp(buf, msg) == 0);
    EXPECT(!vcall<bool>(remote, 0, "../escape.bin", (const void *)msg, (int32_t)sizeof msg));

    ReadResult rr;
    SteamAPICall_t h = vcall<SteamAPICall_t>(remote, 3, "shim_test.bin", (uint32_t)6, (uint32_t)5);
    EXPECT(h != 0);
    SteamAPI_RegisterCallResult(&rr, h);
    SteamAPI_RunCallbacks();
    EXPECT(rr.hits == 1 && rr.handle == h && rr.last.m_hFileReadAsync == h && rr.last.m_cubRead == 5);
    char part[8] = {0};
    EXPECT(vcall<bool>(remote, 4, h, (void *)part, (uint32_t)5) && strcmp(part, "cloud") == 0);

    int32_t count = vcall<int32_t>(remote, 18);
    bool listed = false;
    for (int32_t i = 0; i < count; i++) {
        int32_t size = 0;
        const char *name = vcall<const char *>(remote, 19, i, &size);
        if (strcmp(name, "shim_test.bin") == 0 && size == (int32_t)sizeof msg) listed = true;
    }
    EXPECT(listed);
    EXPECT(vcall<bool>(remote, 6, "shim_test.bin"));              // FileDelete

    SteamAPI_Shutdown();
    printf("%s: %s\n",
#if defined(__arm64__)
           "arm64",
#else
           "x86_64",
#endif
           g_failures ? "FAILED" : "all checks passed");
    return g_failures != 0;
}
