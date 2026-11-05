// The smaller DLLs: advapi32 (no registry, no event log), ole32 (one apartment, no classes),
// shell32 (folders; nothing is launched), winmm (time; waveIn and the mixers are the audio
// layer's, win-x86/shims/audio, and these only answer what it lacks),
// setupapi (no devices), rpcrt4, dinput8 (the system keyboard and mouse), xinput1_3 (no controller), gdi32 (the
// few calls D3DX's font makes), bink2w32 (stubs for a build without the translated DLL).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "audio/audio.h"
#include "win32_shims.h"

uint32_t w32_heap_alloc(uint32_t size);  // win32_kernel32.c
void w32_heap_free(uint32_t p);

#define E(p, n) {#n, p##_##n}

// --- advapi32 ------------------------------------------------------------------------------------
static void a_RegOpenKeyA(rc_ctx *c) {
  if (w32_log) fprintf(stderr, "w32: RegOpenKey(\"%s\"): no registry\n", w32_cstr(w32_arg(c, 1)) ? w32_cstr(w32_arg(c, 1)) : "");
  w32_ret(c, W32_ERROR_FILE_NOT_FOUND, 3);
}
static void a_RegOpenKeyExA(rc_ctx *c) {
  if (w32_log) fprintf(stderr, "w32: RegOpenKeyEx(\"%s\"): no registry\n", w32_cstr(w32_arg(c, 1)) ? w32_cstr(w32_arg(c, 1)) : "");
  w32_ret(c, W32_ERROR_FILE_NOT_FOUND, 5);
}
static void a_RegOpenKeyExW(rc_ctx *c) { w32_ret(c, W32_ERROR_FILE_NOT_FOUND, 5); }
static void a_RegQueryValueExA(rc_ctx *c) { w32_ret(c, W32_ERROR_FILE_NOT_FOUND, 6); }
static void a_RegQueryValueExW(rc_ctx *c) { w32_ret(c, W32_ERROR_FILE_NOT_FOUND, 6); }
static void a_RegCloseKey(rc_ctx *c) { w32_ret(c, 0, 1); }
static void a_RegisterEventSourceA(rc_ctx *c) { w32_ret(c, 0x00ec0001u, 2); }
static void a_DeregisterEventSource(rc_ctx *c) { w32_ret(c, 1, 1); }
static void a_ReportEventA(rc_ctx *c) {  // (h, type, cat, id, sid, nstrings, size, strings, data)
  uint32_t n = w32_arg(c, 5) & 0xffff, s = w32_arg(c, 7);
  for (uint32_t k = 0; k < n && s; k++) fprintf(stderr, "[event log] %s\n", w32_cstr(w32_rd32(s + 4 * k)));
  w32_ret(c, 1, 9);
}
static void a_SystemFunction036(rc_ctx *c) { arc4random_buf(RC32_H(w32_arg(c, 0)), w32_arg(c, 1)); w32_ret(c, 1, 2); }
static void a_GetUserNameA(rc_ctx *c) {
  uint32_t b = w32_arg(c, 0), n = w32_arg(c, 1);
  if (w32_rd32(n) < 7) { w32_wr32(n, 7); w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); w32_ret(c, 0, 2); return; }
  memcpy(RC32_H(b), "Player", 7);
  w32_wr32(n, 7);
  w32_ret(c, 1, 2);
}
static const w32_export advapi[] = {
    E(a, RegOpenKeyA), E(a, RegOpenKeyExA), E(a, RegOpenKeyExW), E(a, RegQueryValueExA), E(a, RegQueryValueExW),
    E(a, RegCloseKey), E(a, RegisterEventSourceA), E(a, DeregisterEventSource), E(a, ReportEventA),
    E(a, SystemFunction036), E(a, GetUserNameA),
};
W32_DLL(w32_dll_advapi32, "advapi32.dll", advapi, 0);

// --- ole32 / oleaut32 ----------------------------------------------------------------------------
static void guid_str(uint32_t g, char *s) {
  const uint8_t *b = (const uint8_t *)RC32_H(g);
  uint32_t d1;
  uint16_t d2, d3;
  memcpy(&d1, b, 4);
  memcpy(&d2, b + 4, 2);
  memcpy(&d3, b + 6, 2);
  sprintf(s, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", d1, d2, d3, b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
static void o_CoInitialize(rc_ctx *c) { w32_ret(c, 0, 1); }
static void o_CoInitializeEx(rc_ctx *c) { w32_ret(c, 0, 2); }
static void o_CoUninitialize(rc_ctx *c) { w32_ret(c, 0, 0); }
static void o_CoCreateInstance(rc_ctx *c) {  // (clsid, outer, ctx, iid, ppv)
  if (wasapi_has_class(w32_arg(c, 0))) {  // the MMDevice enumerator: the audio layer (WASAPI on CoreAudio)
    w32_ret(c, wasapi_cocreate(w32_arg(c, 0), w32_arg(c, 3), w32_arg(c, 4)), 5);
    return;
  }
  char cl[48], ii[48];
  guid_str(w32_arg(c, 0), cl);
  guid_str(w32_arg(c, 3), ii);
  fprintf(stderr, "w32: CoCreateInstance(%s, %s): class not registered\n", cl, ii);
  if (w32_arg(c, 4)) w32_wr32(w32_arg(c, 4), 0);
  w32_ret(c, 0x80040154u, 5);  // REGDB_E_CLASSNOTREG
}
static void o_CoTaskMemAlloc(rc_ctx *c) { w32_ret(c, w32_heap_alloc(w32_arg(c, 0)), 1); }
static void o_CoTaskMemFree(rc_ctx *c) {
  uint32_t p = w32_arg(c, 0);
  if (wa_taskmem_owns(p)) wa_taskmem_free(p);  // what the audio layer's objects handed out
  else w32_heap_free(p);
  w32_ret(c, 0, 1);
}
// one apartment: "marshalling" an interface to another thread hands over the pointer itself
static void o_CoMarshalInterThreadInterfaceInStream(rc_ctx *c) {
  uint32_t unk = w32_arg(c, 1), out = w32_arg(c, 2);
  if (unk) {  // the stream holds a reference (IUnknown::AddRef)
    uint32_t a = unk;
    w32_guest_call(c, w32_rd32(w32_rd32(unk) + 4), W32_STDCALL, 0, &a, 1, 0);
  }
  w32_wr32(out, unk);
  w32_ret(c, 0, 3);
}
static void o_CoGetInterfaceAndReleaseStream(rc_ctx *c) {  // the stream's reference becomes the caller's
  w32_wr32(w32_arg(c, 2), w32_arg(c, 0));
  w32_ret(c, 0, 3);
}
static void o_CoSetProxyBlanket(rc_ctx *c) { w32_ret(c, 0, 8); }
static void o_PropVariantClear(rc_ctx *c) { w32_ret(c, wa_propvariant_clear(w32_arg(c, 0)), 1); }
static const w32_export ole[] = {
    E(o, CoInitialize), E(o, CoInitializeEx), E(o, CoUninitialize), E(o, CoCreateInstance), E(o, CoTaskMemAlloc),
    E(o, CoTaskMemFree), E(o, CoMarshalInterThreadInterfaceInStream), E(o, CoGetInterfaceAndReleaseStream),
    E(o, PropVariantClear), E(o, CoSetProxyBlanket),
};
W32_DLL(w32_dll_ole32, "ole32.dll", ole, 0);
// a BSTR: the byte length in the word before the characters (SysFreeString frees from there)
static void o_SysAllocString(rc_ctx *c) {
  uint32_t s = w32_arg(c, 0), n = 0, p = 0;
  if (s) {
    while (*(const uint16_t *)RC32_H(s + 2 * n)) n++;
    uint32_t b = w32_heap_alloc(4 + 2 * n + 2);
    if (b) {
      w32_wr32(b, 2 * n);
      memcpy(RC32_H(b + 4), RC32_H(s), 2 * n + 2);
      p = b + 4;
    }
  }
  w32_ret(c, p, 1);
}
static void o_SysFreeString(rc_ctx *c) { if (w32_arg(c, 0)) w32_heap_free(w32_arg(c, 0) - 4); w32_ret(c, 0, 1); }
static void o_VariantInit(rc_ctx *c) { memset(RC32_H(w32_arg(c, 0)), 0, 16); w32_ret(c, 0, 1); }
static void o_VariantClear(rc_ctx *c) { memset(RC32_H(w32_arg(c, 0)), 0, 16); w32_ret(c, 0, 1); }
static const w32_export oleaut[] = {E(o, SysAllocString), E(o, SysFreeString),E(o, VariantInit), E(o, VariantClear)};
W32_DLL(w32_dll_oleaut32, "oleaut32.dll", oleaut, 0);

// --- shell32 -------------------------------------------------------------------------------------
static const char *folder(uint32_t csidl) {
  switch (csidl & 0xff) {
  case 0x05: return "C:\\Users\\Player\\Documents";        // CSIDL_PERSONAL
  case 0x1a: return "C:\\Users\\Player\\AppData\\Roaming";  // CSIDL_APPDATA
  case 0x1c: return "C:\\Users\\Player\\AppData\\Local";    // CSIDL_LOCAL_APPDATA
  case 0x23: return "C:\\ProgramData";                      // CSIDL_COMMON_APPDATA
  case 0x2e: return "C:\\Users\\Public\\Documents";         // CSIDL_COMMON_DOCUMENTS
  case 0x28: return "C:\\Users\\Player";                    // CSIDL_PROFILE
  case 0x10: return "C:\\Users\\Player\\Desktop";           // CSIDL_DESKTOPDIRECTORY
  case 0x26: return "C:\\Program Files (x86)";              // CSIDL_PROGRAM_FILES
  case 0x24: return "C:\\Windows";                          // CSIDL_WINDOWS
  case 0x25: case 0x29: return "C:\\Windows\\SysWOW64";     // CSIDL_SYSTEM(X86)
  }
  return 0;
}
static void mkdirs_win(const char *win) {  // every directory of the path, on the host
  char p[512], host[1024];
  snprintf(p, sizeof p, "%s", win);
  for (char *s = p + 3;; s++) {
    if (*s == '\\' || !*s) {
      char ch = *s;
      *s = 0;
      mkdir(w32_path(p, host, sizeof host), 0755);
      if (!ch) break;
      *s = ch;
    }
  }
}
static void s_SHGetFolderPathA(rc_ctx *c) {
  uint32_t csidl = w32_arg(c, 1), out = w32_arg(c, 4);
  const char *f = folder(csidl);
  if (!f) {
    fprintf(stderr, "w32: SHGetFolderPath(0x%x): not known\n", csidl);
    w32_ret(c, 0x80070057u, 5);  // E_INVALIDARG
    return;
  }
  mkdirs_win(f);  // they exist on a real system
  snprintf((char *)RC32_H(out), 260, "%s", f);
  if (w32_log) fprintf(stderr, "w32: SHGetFolderPath(0x%x) -> \"%s\"\n", csidl, f);
  w32_ret(c, 0, 5);
}
static void s_SHGetFolderPathW(rc_ctx *c) {
  const char *f = folder(w32_arg(c, 1));
  if (!f) { w32_ret(c, 0x80070057u, 5); return; }
  mkdirs_win(f);
  w32_put_wstr(w32_arg(c, 4), 260, f);
  w32_ret(c, 0, 5);
}
static void s_ShellExecuteA(rc_ctx *c) {  // never opens anything: no browser, no store page
  fprintf(stderr, "w32: ShellExecute(\"%s\", \"%s\") refused\n", w32_cstr(w32_arg(c, 1)) ? w32_cstr(w32_arg(c, 1)) : "",
          w32_cstr(w32_arg(c, 2)) ? w32_cstr(w32_arg(c, 2)) : "");
  w32_ret(c, 5, 6);  // SE_ERR_ACCESSDENIED
}
static const w32_export shell[] = {E(s, SHGetFolderPathA), E(s, SHGetFolderPathW), E(s, ShellExecuteA)};
W32_DLL(w32_dll_shell32, "shell32.dll", shell, 0);

// --- winmm ---------------------------------------------------------------------------------------
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_INVALHANDLE 5
#define MMSYSERR_NODRIVER 6
static void m_timeGetTime(rc_ctx *c) { w32_ret(c, (uint32_t)(clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000), 0); }
static void m_timeBeginPeriod(rc_ctx *c) { w32_ret(c, 0, 1); }
static void m_timeEndPeriod(rc_ctx *c) { w32_ret(c, 0, 1); }
static void m_waveInGetNumDevs(rc_ctx *c) { w32_ret(c, 0, 0); }
static void m_waveInGetDevCapsA(rc_ctx *c) { w32_ret(c, MMSYSERR_BADDEVICEID, 3); }
static void m_waveInGetID(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 2); }
static void m_waveInOpen(rc_ctx *c) { w32_ret(c, MMSYSERR_BADDEVICEID, 6); }
static void m_waveInClose(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 1); }
static void m_waveInStart(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 1); }
static void m_waveInStop(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 1); }
static void m_waveInPrepareHeader(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 3); }
static void m_waveInAddBuffer(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 3); }
static void m_mixerOpen(rc_ctx *c) { w32_ret(c, MMSYSERR_NODRIVER, 5); }
static void m_mixerClose(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 1); }
static void m_mixerGetLineInfoA(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 3); }
static void m_mixerGetLineControlsA(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 3); }
static void m_mixerGetControlDetailsA(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 3); }
static void m_mixerSetControlDetails(rc_ctx *c) { w32_ret(c, MMSYSERR_INVALHANDLE, 3); }
static const w32_export winmm[] = {
    E(m, timeGetTime), E(m, timeBeginPeriod), E(m, timeEndPeriod), E(m, waveInGetNumDevs), E(m, waveInGetDevCapsA),
    E(m, waveInGetID), E(m, waveInOpen), E(m, waveInClose), E(m, waveInStart), E(m, waveInStop),
    E(m, waveInPrepareHeader), E(m, waveInAddBuffer), E(m, mixerOpen), E(m, mixerClose), E(m, mixerGetLineInfoA),
    E(m, mixerGetLineControlsA), E(m, mixerGetControlDetailsA), E(m, mixerSetControlDetails),
};
W32_DLL(w32_dll_winmm, "winmm.dll", winmm, 0);

// --- setupapi: a device set with nothing in it ---------------------------------------------------
static void d_SetupDiGetClassDevsA(rc_ctx *c) { w32_ret(c, 0x005d0001u, 4); }
static void d_SetupDiGetClassDevsW(rc_ctx *c) { w32_ret(c, 0x005d0001u, 4); }
static void d_SetupDiEnumDeviceInterfaces(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0, 5); }
static void d_SetupDiGetDeviceInterfaceDetailW(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0, 6); }
static void d_SetupDiGetDeviceInterfaceDetailA(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0, 6); }
static void d_SetupDiGetDeviceInterfaceAlias(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0, 4); }
static void d_SetupDiGetDeviceRegistryPropertyW(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0, 7); }
static void d_SetupDiOpenDeviceInterfaceRegKey(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0xffffffffu, 4); }
static void d_SetupDiDestroyDeviceInfoList(rc_ctx *c) { w32_ret(c, 1, 1); }
static const w32_export setupapi[] = {
    E(d, SetupDiGetClassDevsA), E(d, SetupDiGetClassDevsW), E(d, SetupDiEnumDeviceInterfaces),
    E(d, SetupDiGetDeviceInterfaceDetailW), E(d, SetupDiGetDeviceInterfaceDetailA),
    E(d, SetupDiGetDeviceInterfaceAlias), E(d, SetupDiDestroyDeviceInfoList),
    E(d, SetupDiGetDeviceRegistryPropertyW), E(d, SetupDiOpenDeviceInterfaceRegKey),
};
W32_DLL(w32_dll_setupapi, "setupapi.dll", setupapi, 0);

// --- rpcrt4 --------------------------------------------------------------------------------------
static void r_UuidToStringA(rc_ctx *c) {
  char s[48];
  guid_str(w32_arg(c, 0), s);
  for (char *p = s; *p; p++) *p = (char)(*p >= 'A' && *p <= 'F' ? *p + 32 : *p);
  s[37] = 0;  // without the braces
  uint32_t g = w32_heap_alloc(37);
  memcpy(RC32_H(g), s + 1, 37);
  w32_wr32(w32_arg(c, 1), g);
  w32_ret(c, 0, 2);
}
static void r_RpcStringFreeA(rc_ctx *c) {
  uint32_t p = w32_arg(c, 0);
  if (p && w32_rd32(p)) { w32_heap_free(w32_rd32(p)); w32_wr32(p, 0); }
  w32_ret(c, 0, 1);
}
static void r_UuidCreate(rc_ctx *c) {
  uint8_t *b = (uint8_t *)RC32_H(w32_arg(c, 0));
  arc4random_buf(b, 16);
  b[7] = (uint8_t)((b[7] & 0x0f) | 0x40);
  b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);
  w32_ret(c, 0, 1);
}
static const w32_export rpc[] = {E(r, UuidToStringA), E(r, RpcStringFreeA), E(r, UuidCreate)};
W32_DLL(w32_dll_rpcrt4, "rpcrt4.dll", rpc, 0);

// --- dinput8: the system keyboard and mouse ---------------------------------------------------
// The game will not start without them: its input layer enumerates the devices, creates the
// system keyboard and fails its start-up when there is none. State and buffered events come from
// the host window (win32_input.c).
#define DI_OK 0
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_UNSUPPORTED 0x80004001u
static const uint8_t guid_syskeyboard[16] = {0x40, 0xd1, 0x6e, 0x6f, 0x0e, 0xc6, 0xcf, 0x11,
                                             0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
static const uint8_t guid_sysmouse[16] = {0xe0, 0xd1, 0x6e, 0x6f, 0x0e, 0xc6, 0xcf, 0x11,
                                          0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
static const struct di_dev {
  const uint8_t *guid;
  uint32_t type;  // DI8DEVTYPE_* | subtype << 8
  uint32_t axes, buttons;
  const char *name;
} di_devs[] = {
    {guid_syskeyboard, 0x13 | 4 << 8, 0, 128, "Keyboard"},  // DI8DEVTYPE_KEYBOARD, PCENH
    {guid_sysmouse, 0x12 | 1 << 8, 3, 4, "Mouse"},          // DI8DEVTYPE_MOUSE, UNKNOWN
};
#define DI_NDEVS (int)(sizeof di_devs / sizeof di_devs[0])
static int di_wide;  // the IDirectInput8W face: device names in UTF-16
static const struct di_dev *di_dev_of_guid(uint32_t g) {
  for (int k = 0; k < DI_NDEVS; k++)
    if (g && !memcmp(RC32_H(g), di_devs[k].guid, 16)) return &di_devs[k];
  return 0;
}
// DIDEVICEINSTANCEA (0x244 bytes) / W (0x44c): size, instance and product GUID, type, names,
// force-feedback driver GUID, HID usage page and usage
static void di_instance(uint32_t g, const struct di_dev *d) {
  uint32_t n = di_wide ? 0x44c : 0x244, names = di_wide ? 520 : 260;
  memset(RC32_H(g), 0, n);
  w32_wr32(g, n);
  memcpy(RC32_H(g + 4), d->guid, 16);
  memcpy(RC32_H(g + 20), d->guid, 16);
  w32_wr32(g + 36, d->type);
  for (int k = 0; k < 2; k++) {
    if (di_wide) w32_put_wstr(g + 40 + k * names, 260, d->name);
    else snprintf((char *)RC32_H(g + 40 + k * names), 260, "%s", d->name);
  }
}
static const struct di_dev *di_dev_of(uint32_t self) { return &di_devs[w32_rd32(self + 4)]; }
static uint64_t dd_GetCapabilities(rc_ctx *c, uint32_t s, const uint32_t *a) {  // DIDEVCAPS
  (void)c; (void)s;
  const struct di_dev *d = di_dev_of(a[0]);
  uint32_t g = a[1];
  w32_wr32(g + 4, 1);  // DIDC_ATTACHED
  w32_wr32(g + 8, d->type);
  w32_wr32(g + 12, d->axes);
  w32_wr32(g + 16, d->buttons);
  w32_wr32(g + 20, 0);  // POVs
  return DI_OK;
}
static int di_is_mouse(uint32_t self) { return di_dev_of(self)->guid == guid_sysmouse; }
static uint64_t dd_GetDeviceState(rc_ctx *c, uint32_t s, const uint32_t *a) {  // (cbData, lpvData)
  (void)c; (void)s;
  if (!a[2]) return DI_OK;
  memset(RC32_H(a[2]), 0, a[1]);
  if (di_is_mouse(a[0])) w32_input_mouse_state(a[2], a[1]);
  else w32_input_keyboard_state(a[2], a[1]);
  return DI_OK;
}
static uint64_t dd_GetDeviceData(rc_ctx *c, uint32_t s, const uint32_t *a) {  // (cbObjectData, rgdod, pdwInOut, flags)
  (void)c; (void)s;
  if (!a[3]) return 0x80070057u;  // DIERR_INVALIDPARAM
  w32_wr32(a[3], w32_input_events(di_is_mouse(a[0]), a[2], a[1] ? a[1] : 16, w32_rd32(a[3]), a[4]));
  return DI_OK;
}
static uint64_t dd_GetDeviceInfo(rc_ctx *c, uint32_t s, const uint32_t *a) {
  (void)c; (void)s;
  di_instance(a[1], di_dev_of(a[0]));
  return DI_OK;
}
static uint64_t dd_unsupported(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)c; (void)s; (void)a; return DIERR_UNSUPPORTED; }
static uint64_t di_ok(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)c; (void)s; (void)a; return 0; }
static uint64_t di_ref(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)c; (void)s; (void)a; return 1; }
static uint64_t di_noint(rc_ctx *c, uint32_t s, const uint32_t *a) {
  (void)c; (void)s;
  if (a[2]) w32_wr32(a[2], 0);
  return 0x80004002u;  // E_NOINTERFACE
}
static const w32_slot did8_slots[] = {
    {"IDirectInputDevice8::QueryInterface", 3, 0, di_noint}, {"IDirectInputDevice8::AddRef", 1, 0, di_ref},
    {"IDirectInputDevice8::Release", 1, 0, di_ref},
    {"IDirectInputDevice8::GetCapabilities", 2, 0, dd_GetCapabilities},
    {"IDirectInputDevice8::EnumObjects", 4, 0, di_ok}, {"IDirectInputDevice8::GetProperty", 3, 0, dd_unsupported},
    {"IDirectInputDevice8::SetProperty", 3, 0, di_ok}, {"IDirectInputDevice8::Acquire", 1, 0, di_ok},
    {"IDirectInputDevice8::Unacquire", 1, 0, di_ok},
    {"IDirectInputDevice8::GetDeviceState", 3, 0, dd_GetDeviceState},
    {"IDirectInputDevice8::GetDeviceData", 5, 0, dd_GetDeviceData},
    {"IDirectInputDevice8::SetDataFormat", 2, 0, di_ok}, {"IDirectInputDevice8::SetEventNotification", 2, 0, di_ok},
    {"IDirectInputDevice8::SetCooperativeLevel", 3, 0, di_ok},
    {"IDirectInputDevice8::GetObjectInfo", 4, 0, dd_unsupported},
    {"IDirectInputDevice8::GetDeviceInfo", 2, 0, dd_GetDeviceInfo},
    {"IDirectInputDevice8::RunControlPanel", 3, 0, di_ok}, {"IDirectInputDevice8::Initialize", 4, 0, di_ok},
    {"IDirectInputDevice8::CreateEffect", 5, 0, dd_unsupported}, {"IDirectInputDevice8::EnumEffects", 4, 0, di_ok},
    {"IDirectInputDevice8::GetEffectInfo", 3, 0, dd_unsupported},
    {"IDirectInputDevice8::GetForceFeedbackState", 2, 0, dd_unsupported},
    {"IDirectInputDevice8::SendForceFeedbackCommand", 2, 0, dd_unsupported},
    {"IDirectInputDevice8::EnumCreatedEffectObjects", 4, 0, di_ok},
    {"IDirectInputDevice8::Escape", 2, 0, dd_unsupported}, {"IDirectInputDevice8::Poll", 1, 0, di_ok},
    {"IDirectInputDevice8::SendDeviceData", 5, 0, dd_unsupported},
    {"IDirectInputDevice8::EnumEffectsInFile", 5, 0, di_ok},
    {"IDirectInputDevice8::WriteEffectToFile", 5, 0, dd_unsupported},
    {"IDirectInputDevice8::BuildActionMap", 4, 0, dd_unsupported},
    {"IDirectInputDevice8::SetActionMap", 4, 0, dd_unsupported},
    {"IDirectInputDevice8::GetImageInfo", 2, 0, dd_unsupported},
};
static uint64_t di_CreateDevice(rc_ctx *c, uint32_t s, const uint32_t *a) {  // (rguid, ppDevice, outer)
  (void)c; (void)s;
  const struct di_dev *d = di_dev_of_guid(a[1]);
  if (!d) {
    if (a[2]) w32_wr32(a[2], 0);
    return DIERR_DEVICENOTREG;
  }
  uint32_t o = w32_object(w32_vtable(did8_slots, 32, W32_STDCALL, DIERR_UNSUPPORTED), 16);
  w32_wr32(o + 4, (uint32_t)(d - di_devs));
  w32_wr32(a[2], o);
  return DI_OK;
}
// (devType, callback, ref, flags): devType a DI8DEVCLASS_* (0 all, 1 device, 2 pointer, 3
// keyboard, 4 game controller) or a DI8DEVTYPE_*; the callback is stdcall BOOL (instance, ref)
static uint64_t di_EnumDevices(rc_ctx *c, uint32_t s, const uint32_t *a) {
  (void)s;
  uint32_t want = a[1], cb = a[2], ref = a[3];
  uint32_t inst = w32_galloc(0x45c);
  for (int k = 0; k < DI_NDEVS; k++) {
    uint32_t t = di_devs[k].type & 0xff;
    int cls = t == 0x13 ? 3 : t == 0x12 ? 2 : 1;
    if (want && want != (uint32_t)cls && !(want >= 0x11 && want == t)) continue;
    di_instance(inst, &di_devs[k]);
    uint32_t args[2] = {inst, ref};
    if (!w32_guest_call(c, cb, W32_STDCALL, 0, args, 2, 0)) break;  // DIENUM_STOP
  }
  w32_gfree(inst, 0x45c);
  return DI_OK;
}
static uint64_t di_GetDeviceStatus(rc_ctx *c, uint32_t s, const uint32_t *a) {
  (void)c; (void)s;
  return di_dev_of_guid(a[1]) ? DI_OK : DIERR_DEVICENOTREG;
}
static const w32_slot di8_slots[] = {
    {"IDirectInput8::QueryInterface", 3, 0, di_noint}, {"IDirectInput8::AddRef", 1, 0, di_ref},
    {"IDirectInput8::Release", 1, 0, di_ref}, {"IDirectInput8::CreateDevice", 4, 0, di_CreateDevice},
    {"IDirectInput8::EnumDevices", 5, 0, di_EnumDevices}, {"IDirectInput8::GetDeviceStatus", 2, 0, di_GetDeviceStatus},
    {"IDirectInput8::RunControlPanel", 3, 0, di_ok}, {"IDirectInput8::Initialize", 3, 0, di_ok},
    {"IDirectInput8::FindDevice", 4, 0, 0}, {"IDirectInput8::EnumDevicesBySemantics", 6, 0, di_ok},
    {"IDirectInput8::ConfigureDevices", 5, 0, di_ok},
};
static void i_DirectInput8Create(rc_ctx *c) {  // (hinst, version, riid, ppv, outer)
  static uint32_t obj;
  static const uint8_t iid_w[16] = {0x31, 0x80, 0x79, 0xbf, 0x3a, 0x48, 0xa2, 0x4d,
                                    0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0x00};
  di_wide = w32_arg(c, 2) && !memcmp(RC32_H(w32_arg(c, 2)), iid_w, 16);
  if (!obj) obj = w32_object(w32_vtable(di8_slots, 11, W32_STDCALL, 0x80004001u), 16);
  w32_wr32(w32_arg(c, 3), obj);
  w32_ret(c, 0, 5);
}
static const w32_export dinput[] = {E(i, DirectInput8Create)};
W32_DLL(w32_dll_dinput8, "dinput8.dll", dinput, 0);

// --- xinput1_3: no controller connected ----------------------------------------------------------
static void x_XInputGetState(rc_ctx *c) { w32_ret(c, W32_ERROR_DEVICE_NOT_CONNECTED, 2); }
static void x_XInputSetState(rc_ctx *c) { w32_ret(c, W32_ERROR_DEVICE_NOT_CONNECTED, 2); }
static void x_XInputGetCapabilities(rc_ctx *c) { w32_ret(c, W32_ERROR_DEVICE_NOT_CONNECTED, 3); }
static void x_XInputEnable(rc_ctx *c) { w32_ret(c, 0, 1); }
static void x_XInputGetDSoundAudioDeviceGuids(rc_ctx *c) { w32_ret(c, W32_ERROR_DEVICE_NOT_CONNECTED, 3); }
static void x_XInputGetBatteryInformation(rc_ctx *c) { w32_ret(c, W32_ERROR_DEVICE_NOT_CONNECTED, 3); }
static void x_XInputGetKeystroke(rc_ctx *c) { w32_ret(c, W32_ERROR_DEVICE_NOT_CONNECTED, 3); }
static const w32_export xinput[] = {
    E(x, XInputGetState), E(x, XInputSetState), E(x, XInputGetCapabilities), E(x, XInputEnable),
    E(x, XInputGetDSoundAudioDeviceGuids), E(x, XInputGetBatteryInformation), E(x, XInputGetKeystroke),
};
W32_DLL(w32_dll_xinput1_3, "xinput1_3.dll", xinput, 0);

// --- avrt: MMCSS accepted and ignored -------------------------------------------------------------
// PortAudio's WASAPI host API starts only when avrt.dll loads with all six of these (its "not
// Vista?" check), else the game finds no output device. The audio layer runs its own real-time
// threads, so the thread characteristics change nothing.
static void v_AvSetMmThreadCharacteristicsA(rc_ctx *c) {  // (task name, &task index) -> a handle
  if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), 1);
  w32_ret(c, 0x00a70001u, 2);
}
static void v_AvSetMmThreadCharacteristicsW(rc_ctx *c) { v_AvSetMmThreadCharacteristicsA(c); }
static void v_AvRevertMmThreadCharacteristics(rc_ctx *c) { w32_ret(c, 1, 1); }
static void v_AvSetMmThreadPriority(rc_ctx *c) { w32_ret(c, 1, 2); }
// thread ordering groups: none can be made (PortAudio does not need them)
static void v_AvRtCreateThreadOrderingGroup(rc_ctx *c) { w32_set_error(c, W32_ERROR_NOT_SUPPORTED); w32_ret(c, 0, 4); }
static void v_AvRtDeleteThreadOrderingGroup(rc_ctx *c) { w32_ret(c, 1, 1); }
static void v_AvRtWaitOnThreadOrderingGroup(rc_ctx *c) { w32_ret(c, 1, 1); }
static const w32_export avrt[] = {
    E(v, AvSetMmThreadCharacteristicsA), E(v, AvSetMmThreadCharacteristicsW), E(v, AvRevertMmThreadCharacteristics),
    E(v, AvSetMmThreadPriority), E(v, AvRtCreateThreadOrderingGroup), E(v, AvRtDeleteThreadOrderingGroup),
    E(v, AvRtWaitOnThreadOrderingGroup),
};
W32_DLL(w32_dll_avrt, "avrt.dll", avrt, 0);

// --- gdi32 ---------------------------------------------------------------------------------------
static void g_CreateCompatibleDC(rc_ctx *c) { w32_ret(c, 0x00dc1000u, 1); }
static void g_DeleteDC(rc_ctx *c) { w32_ret(c, 1, 1); }
static void g_DeleteObject(rc_ctx *c) { w32_ret(c, 1, 1); }
static void g_SelectObject(rc_ctx *c) { w32_ret(c, 0, 2); }
static void g_GetDeviceCaps(rc_ctx *c) {
  uint32_t i = w32_arg(c, 1);
  w32_ret(c, i == 88 || i == 90 ? 96 : i == 12 ? 32 : i == 116 ? 60 : 0, 2);  // LOGPIXELSX/Y, BITSPIXEL, VREFRESH
}
static const w32_export gdi[] = {E(g, CreateCompatibleDC), E(g, DeleteDC), E(g, DeleteObject), E(g, SelectObject), E(g, GetDeviceCaps)};
W32_DLL(w32_dll_gdi32, "gdi32.dll", gdi, 0);

// --- bink2w32 stubs, for a build without the translated DLL: no video opens ----------------------
static void b_BinkOpen(rc_ctx *c) {
  fprintf(stderr, "w32: BinkOpen(\"%s\", 0x%x): no video player\n", w32_cstr(w32_arg(c, 0)) ? w32_cstr(w32_arg(c, 0)) : "", w32_arg(c, 1));
  w32_ret(c, 0, 2);
}
static void b_BinkSetIO(rc_ctx *c) { w32_ret(c, 0, 1); }
static void b_BinkSetIOSize(rc_ctx *c) { w32_ret(c, 0, 1); }
static void b_BinkSetSoundTrack(rc_ctx *c) { w32_ret(c, 0, 2); }
static void b_BinkClose(rc_ctx *c) { w32_ret(c, 0, 1); }
static void b_BinkCloseTrack(rc_ctx *c) { w32_ret(c, 0, 1); }
static void b_BinkCopyToBuffer(rc_ctx *c) { w32_ret(c, 1, 7); }
static void b_BinkDoFrame(rc_ctx *c) { w32_ret(c, 0, 1); }
static void b_BinkGetSummary(rc_ctx *c) { w32_ret(c, 0, 2); }
static void b_BinkGetTrackData(rc_ctx *c) { w32_ret(c, 0, 2); }
static void b_BinkGetTrackID(rc_ctx *c) { w32_ret(c, 0, 2); }
static void b_BinkNextFrame(rc_ctx *c) { w32_ret(c, 0, 1); }
static void b_BinkOpenTrack(rc_ctx *c) { w32_ret(c, 0, 2); }
static void b_BinkWait(rc_ctx *c) { w32_ret(c, 0, 1); }
#define B(n, w) {"_" #n "@" #w, b_##n}
static const w32_export bink[] = {
    B(BinkOpen, 8), B(BinkSetIO, 4), B(BinkSetIOSize, 4), B(BinkSetSoundTrack, 8), B(BinkClose, 4),
    B(BinkCloseTrack, 4), B(BinkCopyToBuffer, 28), B(BinkDoFrame, 4), B(BinkGetSummary, 8), B(BinkGetTrackData, 8),
    B(BinkGetTrackID, 8), B(BinkNextFrame, 4), B(BinkOpenTrack, 8), B(BinkWait, 4),
};
W32_DLL(w32_dll_bink2w32, "bink2w32.dll", bink, 0);
