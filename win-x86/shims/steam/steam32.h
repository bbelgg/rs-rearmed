// steam_api.dll for the 32-bit Windows guest, offline (win-x86).
//
// The counterpart of the Mac stub (shims/steam): the flat exports the game imports, the Steam
// interfaces as guest objects whose vtables are MSVC thiscall thunks (layouts: steam32_slots.h,
// read from the game's own steam_api.dll), and a local callback / call-result dispatcher run by
// SteamAPI_RunCallbacks. The behaviour is the Mac stub's stub mode: an offline user, the owned
// DLC list, Steam Cloud in a local directory, local stats. Nothing talks to Steam or the network.
#ifndef STEAM32_H
#define STEAM32_H

#include "rc32.h"

// The shim of an import "steam_api.dll!<name>" (the DLL part matched without case), or 0. Every
// steam_api export is cdecl: the shim leaves its arguments on the stack.
rc_fn steam32_import(const char *import_name);

// 1 for a DLL the game must not load: the Uplay / Ubisoft Connect loaders and Valve's own client
// libraries. LoadLibrary of one fails with ERROR_MOD_NOT_FOUND, which the game takes as "not
// installed". Takes a bare or full path, either slash.
int steam32_dll_blocked(const char *dll_name);

// For tests: the guest object for an interface version string (0 when unknown).
uint32_t steam32_interface(const char *version);

#endif
