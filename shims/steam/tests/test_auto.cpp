// Mode "auto" with a Steam library that loads and whose Init fails: the listeners registered before
// SteamAPI_Init are the stub's afterwards, and one unregistered before it is not.
// Run with RS_STEAM_MODE=auto RS_STEAM_REAL_LIB=<fake_real's library>.
#include <cstdint>
#include <cstdio>
#include <cstring>

typedef int32_t HSteamUser;
typedef uint64_t SteamAPICall_t;
extern "C" {
bool SteamAPI_Init();
void SteamAPI_RunCallbacks();
HSteamUser SteamAPI_GetHSteamUser();
void SteamAPI_RegisterCallback(void *, int);
void SteamAPI_UnregisterCallback(void *);
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
    void Run(void *) override { hits++; }
    void Run(void *p, bool, SteamAPICall_t) override { Run(p); }
    int GetCallbackSizeBytes() override { return 20; }
};

int main() {
    StatsListener kept, dropped;
    SteamAPI_RegisterCallback(&kept, 1101);     // UserStatsReceived_t
    SteamAPI_RegisterCallback(&dropped, 1101);
    SteamAPI_UnregisterCallback(&dropped);
    EXPECT(SteamAPI_Init());                    // the library's fails: the stub's
    void *stats = SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), "STEAMUSERSTATS_INTERFACE_VERSION011");
    EXPECT(stats != nullptr);
    if (stats) {
        typedef bool (*Fn)(void *);
        EXPECT(((Fn)(*(void ***)stats)[0])(stats));  // RequestCurrentStats
    }
    SteamAPI_RunCallbacks();
    EXPECT(kept.hits == 1 && dropped.hits == 0);
    EXPECT(kept.m_nCallbackFlags & 1);
    SteamAPI_UnregisterCallback(&kept);
    if (g_failures) return 1;
    printf("test_auto: OK\n");
    return 0;
}
