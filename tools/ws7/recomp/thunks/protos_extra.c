// Prototypes for imports that no SDK header declares: compiler/C++ runtime support, Steam's flat API
// (the Steamworks SDK is not in the tree) and the removed Carbon alert calls. The project's own shim
// libraries are described by their headers (bink2_abi.h, cg_api.h). Parsed by sigs.py; never compiled to code.
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "bink2_abi.h"
#include "cg_api.h"

// C++ ABI (libc++abi / libstdc++)
int __cxa_atexit(void (*)(void *), void *, void *);
void *__cxa_allocate_exception(size_t);
void __cxa_free_exception(void *);
void *__cxa_begin_catch(void *);
void __cxa_end_catch(void);
void __cxa_rethrow(void);
void __cxa_throw(void *, void *, void (*)(void *));
int __cxa_guard_acquire(long long *);
void __cxa_guard_release(long long *);
void __cxa_guard_abort(long long *);
void __cxa_pure_virtual(void);
void *__dynamic_cast(const void *, const void *, const void *, ptrdiff_t);
void _Unwind_Resume(void *);
// libSystem internals
void __bzero(void *, size_t);
void *__memcpy_chk(void *, const void *, size_t, size_t);
void *__memmove_chk(void *, const void *, size_t, size_t);
void *__memset_chk(void *, int, size_t, size_t);
char *__strcat_chk(char *, const char *, size_t);
char *__strcpy_chk(char *, const char *, size_t);
void __stack_chk_fail(void);
unsigned __int128 __udivti3(unsigned __int128, unsigned __int128);
unsigned __int128 __umodti3(unsigned __int128, unsigned __int128);
// Steam flat API
bool SteamAPI_Init(void);
void SteamAPI_Shutdown(void);
void SteamAPI_RunCallbacks(void);
int SteamAPI_GetHSteamUser(void);
bool SteamAPI_RestartAppIfNecessary(uint32_t appid);
void SteamAPI_RegisterCallback(void *callback, int id);
void SteamAPI_UnregisterCallback(void *callback);
void SteamAPI_RegisterCallResult(void *callback, uint64_t call);
void SteamAPI_UnregisterCallResult(void *callback, uint64_t call);
void *SteamInternal_ContextInit(void *data);
void *SteamInternal_CreateInterface(const char *version);
void *SteamInternal_FindOrCreateUserInterface(int user, const char *version);
// Carbon (removed from the SDK)
int32_t CreateStandardAlert(int16_t type, const void *error, const void *explanation, const void *param, void **alert);
int32_t RunStandardAlert(void *alert, void *filter, int16_t *item);
// ObjC runtime (not in the public headers on this SDK)
void *objc_initializeClassPair(void *super, const char *name, void *cls, void *meta);
signed char objc_collectingEnabled(void);
