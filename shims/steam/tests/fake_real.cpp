// A stand-in for Valve's library whose SteamAPI_Init fails (no Steam running): what mode "auto" falls
// back from. It has the exports the layer asks a real library for, and counts the listeners it is given.
#include <cstdint>
#define X extern "C" __attribute__((visibility("default")))
static int g_registered;
X bool SteamAPI_Init() { return false; }
X void SteamAPI_Shutdown() {}
X void SteamAPI_RunCallbacks() {}
X bool SteamAPI_RestartAppIfNecessary(uint32_t) { return false; }
X int32_t SteamAPI_GetHSteamUser() { return 0; }
X void SteamAPI_RegisterCallback(void *, int) { g_registered++; }
X void SteamAPI_UnregisterCallback(void *) { g_registered--; }
X void SteamAPI_RegisterCallResult(void *, uint64_t) {}
X void SteamAPI_UnregisterCallResult(void *, uint64_t) {}
X void *SteamInternal_ContextInit(void *) { static void *none; return &none; }
X void *SteamInternal_CreateInterface(const char *) { return nullptr; }
X void *SteamInternal_FindOrCreateUserInterface(int32_t, const char *) { return nullptr; }
X int fake_real_registered() { return g_registered; }
