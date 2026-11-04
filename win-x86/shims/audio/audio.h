// Learn & Play audio: the WASAPI (MMDevice API) objects and the winmm waveIn / mixer functions
// the game calls, on CoreAudio (docs/windows/DESIGN.md "Audio").
//
// The game reaches audio through PortAudio's WASAPI host API (its Wwise sink and the guitar
// input both open PortAudio streams), and through winmm's waveIn when PortAudio can't start.
// The COM objects are guest vtables of host thunk cells (win32_guestobj.h, W32_STDCALL); the
// winmm functions are stdcall cells too, found by name. Streams run on CoreAudio HAL units with
// the Mac build's latency policy (wa_engine.h, shims/coreaudio/rs_audio_host.h).
#ifndef WIN_AUDIO_H
#define WIN_AUDIO_H

#include <stdint.h>

#include "rc32.h"

// CoCreateInstance for the classes this layer serves: clsid and iid are guest pointers to GUIDs,
// guest_out a guest pointer to the interface pointer. CLSID_MMDeviceEnumerator gives an
// IMMDeviceEnumerator (IID_IMMDeviceEnumerator or IID_IUnknown). Returns an HRESULT;
// REGDB_E_CLASSNOTREG (0x80040154) for any other class, so the caller can go on looking.
uint32_t wasapi_cocreate(uint32_t clsid, uint32_t iid, uint32_t guest_out);
// 1 when the guest GUID at clsid is a class wasapi_cocreate serves.
int wasapi_has_class(uint32_t clsid);

// A winmm export by name ("waveInOpen" or "winmm.dll!waveInOpen", any case): the guest address
// of its stdcall cell (made once), 0 when this layer has no such function. For an import slot
// or GetProcAddress.
uint32_t audio_import(const char *name);
// The same function called in the shims' import convention (win32_shims.h: arguments at guest
// esp, no return address, the shim removes them): 1 when name is one of ours and the call ran.
int audio_dispatch(rc_ctx *c, const char *name);

// CoTaskMemAlloc'd memory the MMDevice objects hand out (endpoint ids, PROPVARIANT strings and
// blobs, GetMixFormat): CoTaskMemFree and PropVariantClear must release it through these
// (a size word in front of each block). wa_taskmem_free ignores 0 and blocks it did not make.
uint32_t wa_taskmem_alloc(uint32_t size);
void wa_taskmem_free(uint32_t g);
int wa_taskmem_owns(uint32_t g);
// PropVariantClear for the types the property store returns (VT_LPWSTR, VT_BLOB, scalars):
// frees the data and zeroes the PROPVARIANT. Returns S_OK.
uint32_t wa_propvariant_clear(uint32_t pv);

#endif
