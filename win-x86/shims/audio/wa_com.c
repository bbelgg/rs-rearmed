// WASAPI face: the MMDevice API objects PortAudio's WASAPI host API calls in the Learn & Play
// game (docs/windows/DESIGN.md "Audio"), as guest vtables of stdcall host
// thunk cells (win32_guestobj.h). A guest object is two words: its vtable and the index of its
// host state in objs[].
//
//   IMMDeviceEnumerator   EnumAudioEndpoints, GetDefaultAudioEndpoint, GetDevice,
//                         Register/UnregisterEndpointNotificationCallback
//   IMMDeviceCollection   GetCount, Item
//   IMMDevice             Activate (IAudioClient, IAudioEndpointVolume), OpenPropertyStore, GetId,
//                         GetState; QueryInterface gives IMMEndpoint (GetDataFlow)
//   IPropertyStore        GetCount, GetAt, GetValue (friendly name, device format, form factor,
//                         and the USB instance id the game reads the cable's VID/PID from)
//   IAudioClient          all of it; event mode signals the guest's event from CoreAudio
//   IAudioRenderClient    GetBuffer, ReleaseBuffer
//   IAudioCaptureClient   GetBuffer, ReleaseBuffer, GetNextPacketSize
//   IAudioEndpointVolume  master / channel volume and mute as a software gain on the stream
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "rs_audio_host.h"
#include "wa_engine.h"
#include "win32_guestobj.h"

#define S_OK 0u
#define S_FALSE 1u
#define E_NOTIMPL 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define E_POINTER 0x80004003u
#define E_INVALIDARG 0x80070057u
#define E_OUTOFMEMORY 0x8007000Eu
#define E_NOTFOUND 0x80070490u
#define REGDB_E_CLASSNOTREG 0x80040154u
#define AUDCLNT_E_NOT_INITIALIZED 0x88890001u
#define AUDCLNT_E_ALREADY_INITIALIZED 0x88890002u
#define AUDCLNT_E_WRONG_ENDPOINT_TYPE 0x88890003u
#define AUDCLNT_E_DEVICE_INVALIDATED 0x88890004u
#define AUDCLNT_E_NOT_STOPPED 0x88890005u
#define AUDCLNT_E_BUFFER_TOO_LARGE 0x88890006u
#define AUDCLNT_E_OUT_OF_ORDER 0x88890007u
#define AUDCLNT_E_UNSUPPORTED_FORMAT 0x88890008u
#define AUDCLNT_E_INVALID_SIZE 0x88890009u
#define AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED 0x88890014u
#define AUDCLNT_E_EVENTHANDLE_NOT_SET 0x88890015u
#define AUDCLNT_S_BUFFER_EMPTY 0x08890001u
#define AUDCLNT_STREAMFLAGS_EVENTCALLBACK 0x40000u
#define AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY 1u
#define AUDCLNT_BUFFERFLAGS_SILENT 2u
#define DEVICE_STATE_ACTIVE 1u
#define VT_EMPTY 0
#define VT_UI4 19
#define VT_LPWSTR 31
#define VT_BLOB 65

// ------------------------------------------------------------------------------------------------
// GUIDs (as they lie in memory)
// ------------------------------------------------------------------------------------------------
#define GUIDB(l, w1, w2, b0, b1, b2, b3, b4, b5, b6, b7)                                                         \
  {(l) & 0xff, ((l) >> 8) & 0xff, ((l) >> 16) & 0xff, ((l) >> 24) & 0xff, (w1) & 0xff, ((w1) >> 8) & 0xff,      \
   (w2) & 0xff, ((w2) >> 8) & 0xff, b0, b1, b2, b3, b4, b5, b6, b7}
typedef uint8_t wguid[16];
static const wguid CLSID_MMDeviceEnumerator = GUIDB(0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
static const wguid IID_IUnknown = GUIDB(0x00000000, 0x0000, 0x0000, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
static const wguid IID_IMMDeviceEnumerator = GUIDB(0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
static const wguid IID_IMMDeviceCollection = GUIDB(0x0BD7A1BE, 0x7A1A, 0x44DB, 0x83, 0x97, 0xCC, 0x53, 0x92, 0x38, 0x7B, 0x5E);
static const wguid IID_IMMDevice = GUIDB(0xD666063F, 0x1587, 0x4E43, 0x81, 0xF1, 0xB9, 0x48, 0xE8, 0x07, 0x36, 0x3F);
static const wguid IID_IMMEndpoint = GUIDB(0x1BE09788, 0x6894, 0x4089, 0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5);
static const wguid IID_IPropertyStore = GUIDB(0x886D8EEB, 0x8CF2, 0x4446, 0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99);
static const wguid IID_IAudioClient = GUIDB(0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
static const wguid IID_IAudioRenderClient = GUIDB(0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
static const wguid IID_IAudioCaptureClient = GUIDB(0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
static const wguid IID_IAudioEndpointVolume = GUIDB(0x5CDF2C82, 0x841E, 0x4546, 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A);
// property keys: fmtid + pid
static const wguid FMTID_Device = GUIDB(0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0);
static const wguid FMTID_DeviceInterface = GUIDB(0x026E516E, 0xB814, 0x414B, 0x83, 0xCD, 0x85, 0x6D, 0x6F, 0xEF, 0x48, 0x22);
static const wguid FMTID_AudioEngine = GUIDB(0xF19F064D, 0x082C, 0x4E27, 0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C);
static const wguid FMTID_AudioEndpoint = GUIDB(0x1DA5D803, 0xD492, 0x4EDD, 0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E);
// the endpoint's device instance ("{1}.USB\VID_12BA&PID_00FF&MI_00\..." on Windows)
static const wguid FMTID_EndpointInstance = GUIDB(0xB3F8FA53, 0x0004, 0x438E, 0x90, 0x03, 0x51, 0xA4, 0x6E, 0x13, 0x9B, 0xFC);

static int guid_eq(uint32_t g, const wguid want) { return g && memcmp(RC32_H(g), want, 16) == 0; }

// ------------------------------------------------------------------------------------------------
// CoTaskMem blocks, strings
// ------------------------------------------------------------------------------------------------
#define TASKMEM_MAGIC 0x4d544157u  // "WATM"

uint32_t wa_taskmem_alloc(uint32_t size) {
  uint32_t g = w32_galloc(size + 8);
  if (!g) return 0;
  w32_wr32(g, TASKMEM_MAGIC);
  w32_wr32(g + 4, size);
  return g + 8;
}

int wa_taskmem_owns(uint32_t g) { return g >= 8 && w32_rd32(g - 8) == TASKMEM_MAGIC; }

void wa_taskmem_free(uint32_t g) {
  if (!g || !wa_taskmem_owns(g)) return;
  uint32_t size = w32_rd32(g - 4);
  w32_wr32(g - 8, 0);
  w32_gfree(g - 8, size + 8);
}

// UTF-8 -> a CoTaskMem UTF-16 string
static uint32_t wstr_new(const char *s) {
  size_t n = strlen(s);
  uint32_t g = wa_taskmem_alloc((uint32_t)(2 * n + 2));
  if (!g) return 0;
  uint16_t *d = w32_h(g);
  const uint8_t *p = (const uint8_t *)s;
  size_t k = 0;
  while (*p) {
    uint32_t cp = *p++;
    if (cp >= 0xc0) {
      int extra = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : 1;
      cp &= 0x3fu >> extra;
      while (extra-- && (*p & 0xc0) == 0x80) cp = cp << 6 | (*p++ & 0x3f);
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      d[k++] = (uint16_t)(0xd800 | cp >> 10);
      d[k++] = (uint16_t)(0xdc00 | (cp & 0x3ff));
    } else {
      d[k++] = (uint16_t)cp;
    }
  }
  d[k] = 0;
  return g;
}

// a guest UTF-16 string -> UTF-8 (BMP is enough for endpoint ids)
static void wstr_get(uint32_t g, char *buf, size_t n) {
  size_t k = 0;
  const uint16_t *s = w32_h(g);
  for (; s && *s && k + 4 < n; s++) {
    uint32_t c = *s;
    if (c < 0x80) buf[k++] = (char)c;
    else if (c < 0x800) { buf[k++] = (char)(0xc0 | c >> 6); buf[k++] = (char)(0x80 | (c & 0x3f)); }
    else { buf[k++] = (char)(0xe0 | c >> 12); buf[k++] = (char)(0x80 | ((c >> 6) & 0x3f)); buf[k++] = (char)(0x80 | (c & 0x3f)); }
  }
  buf[k] = 0;
}

uint32_t wa_propvariant_clear(uint32_t pv) {
  if (!pv) return S_OK;
  uint16_t vt;
  memcpy(&vt, RC32_H(pv), 2);
  if (vt == VT_LPWSTR) wa_taskmem_free(w32_rd32(pv + 8));
  else if (vt == VT_BLOB) wa_taskmem_free(w32_rd32(pv + 12));
  memset(RC32_H(pv), 0, 16);
  return S_OK;
}

// ------------------------------------------------------------------------------------------------
// Endpoint volumes: one software gain per endpoint, kept for the life of the process
// ------------------------------------------------------------------------------------------------
typedef struct {
  char key[200];  // flow + uid
  _Atomic float scalar;
  _Atomic int mute;
} wa_volume;
static wa_volume g_vols[64];
static int g_nvols;
static pthread_mutex_t g_vol_mu = PTHREAD_MUTEX_INITIALIZER;

static wa_volume *volume_of(const wa_endpoint *ep) {
  char key[200];
  snprintf(key, sizeof key, "%d:%s", ep->flow, ep->uid);
  pthread_mutex_lock(&g_vol_mu);
  wa_volume *v = NULL;
  for (int i = 0; i < g_nvols && !v; i++)
    if (!strcmp(g_vols[i].key, key)) v = &g_vols[i];
  if (!v && g_nvols < 64) {
    v = &g_vols[g_nvols++];
    snprintf(v->key, sizeof v->key, "%s", key);
    atomic_store(&v->scalar, 1.f);
  }
  pthread_mutex_unlock(&g_vol_mu);
  return v;
}

static float volume_gain(wa_volume *v) {
  if (!v) return 1.f;
  return atomic_load_explicit(&v->mute, memory_order_relaxed) ? 0.f
                                                              : atomic_load_explicit(&v->scalar, memory_order_relaxed);
}

float wa_endpoint_volume(const wa_endpoint *ep) {
  wa_volume *v = volume_of(ep);
  return v ? atomic_load(&v->scalar) : 1.f;
}
void wa_endpoint_set_volume(const wa_endpoint *ep, float scalar) {
  wa_volume *v = volume_of(ep);
  if (v) atomic_store(&v->scalar, scalar < 0.f ? 0.f : scalar > 1.f ? 1.f : scalar);
}
int wa_endpoint_mute(const wa_endpoint *ep) {
  wa_volume *v = volume_of(ep);
  return v ? atomic_load(&v->mute) : 0;
}
void wa_endpoint_set_mute(const wa_endpoint *ep, int mute) {
  wa_volume *v = volume_of(ep);
  if (v) atomic_store(&v->mute, mute != 0);
}
float wa_endpoint_gain(const wa_endpoint *ep) { return volume_gain(volume_of(ep)); }

// ------------------------------------------------------------------------------------------------
// Objects
// ------------------------------------------------------------------------------------------------
enum { K_ENUM, K_COLL, K_DEVICE, K_ENDPOINT, K_PROPS, K_CLIENT, K_RENDER, K_CAPTURE, K_VOLUME };

typedef struct wa_obj wa_obj;
struct wa_obj {
  int kind;
  uint32_t g;
  _Atomic int refs;
  wa_obj *parent;  // render / capture client -> its audio client; endpoint -> its device
  wa_endpoint ep;
  // collection
  wa_endpoint *items;
  int n_items;
  // audio client
  wa_stream *st;
  wa_fmt fmt;
  int exclusive, event_mode, initialized;
  uint32_t flags, buffer_frames, period_frames, event;
  uint32_t scratch, scratch_frames, pending;  // GetBuffer's guest buffer, frames handed out
  uint32_t overruns_seen;
  wa_volume *vol;
};

#define MAX_OBJS 4096
static wa_obj *g_objs[MAX_OBJS];
static pthread_mutex_t g_obj_mu = PTHREAD_MUTEX_INITIALIZER;

static wa_obj *obj_of(uint32_t g) {
  if (!g) return NULL;
  uint32_t k = w32_rd32(g + 4);
  if (k >= MAX_OBJS) return NULL;
  wa_obj *o = g_objs[k];
  return o && o->g == g ? o : NULL;
}

static uint32_t vtable_of(int kind);

static wa_obj *obj_new(int kind) {
  wa_obj *o = calloc(1, sizeof *o);
  if (!o) return NULL;
  uint32_t vt = vtable_of(kind), g = vt ? w32_galloc(8) : 0;
  if (!g) {
    free(o);
    return NULL;
  }
  o->kind = kind;
  o->g = g;
  atomic_store(&o->refs, 1);
  pthread_mutex_lock(&g_obj_mu);
  uint32_t k = 0;
  while (k < MAX_OBJS && g_objs[k]) k++;
  if (k < MAX_OBJS) g_objs[k] = o;
  pthread_mutex_unlock(&g_obj_mu);
  if (k == MAX_OBJS) {
    w32_gfree(g, 8);
    free(o);
    return NULL;
  }
  w32_wr32(g, vt);
  w32_wr32(g + 4, k);
  return o;
}

static void obj_addref(wa_obj *o) { atomic_fetch_add(&o->refs, 1); }

// thread state of the guest thread that feeds a stream (RS_AUDIO_RT): real-time, in the device's
// workgroup, as the Mac build runs its Wwise thread
static _Thread_local wa_obj *t_rt_client;
static _Thread_local rs_audio_workgroup t_wg;

static void rt_leave(wa_obj *client) {
  if (t_rt_client == client) {
    rs_audio_wg_leave(&t_wg);
    t_rt_client = NULL;
  }
}

static void rt_enter(wa_obj *client) {
  if (t_rt_client == client || !client->st || !rs_audio_env_on("RS_AUDIO_RT", 1)) return;
  const char *who = client->ep.flow == WA_CAPTURE ? "WASAPI capture thread" : "WASAPI render thread";
  wa_stream_info i;
  wa_stream_info_get(client->st, &i);
  rs_audio_make_realtime(i.period_frames, client->fmt.rate, who);
  rs_audio_wg_join(&t_wg, wa_stream_device(client->st), who);
  t_rt_client = client;
}

static void obj_free(wa_obj *o) {
  if (o->kind == K_CLIENT) {
    rt_leave(o);
    wa_stream_close(o->st);
    if (o->scratch) w32_gfree(o->scratch, o->scratch_frames * o->fmt.block);
  }
  if (o->parent) {
    wa_obj *p = o->parent;
    if (atomic_fetch_sub(&p->refs, 1) == 1) obj_free(p);
  }
  free(o->items);
  pthread_mutex_lock(&g_obj_mu);
  g_objs[w32_rd32(o->g + 4)] = NULL;
  pthread_mutex_unlock(&g_obj_mu);
  w32_wr32(o->g, 0);
  w32_gfree(o->g, 8);
  free(o);
}

static void out32(uint32_t g, uint32_t v) {
  if (g) w32_wr32(g, v);
}

// ------------------------------------------------------------------------------------------------
// IUnknown
// ------------------------------------------------------------------------------------------------
static uint32_t iid_ok(wa_obj *o, uint32_t iid);

static uint64_t m_QueryInterface(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *o = obj_of(a[0]);
  uint32_t iid = a[1], ppv = a[2];
  if (!ppv) return E_POINTER;
  w32_wr32(ppv, 0);
  if (!o) return E_NOINTERFACE;
  if (o->kind == K_DEVICE && guid_eq(iid, IID_IMMEndpoint)) {
    wa_obj *e = obj_new(K_ENDPOINT);
    if (!e) return E_OUTOFMEMORY;
    e->ep = o->ep;
    e->parent = o;
    obj_addref(o);
    w32_wr32(ppv, e->g);
    return S_OK;
  }
  if (!iid_ok(o, iid)) return E_NOINTERFACE;
  obj_addref(o);
  w32_wr32(ppv, o->g);
  return S_OK;
}

static uint64_t m_AddRef(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *o = obj_of(a[0]);
  return o ? (uint32_t)(atomic_fetch_add(&o->refs, 1) + 1) : 1;
}

static uint64_t m_Release(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *o = obj_of(a[0]);
  if (!o) return 0;
  int n = atomic_fetch_sub(&o->refs, 1) - 1;
  if (n == 0) obj_free(o);
  return (uint32_t)(n < 0 ? 0 : n);
}

#define IUNKNOWN {"QueryInterface", 3, 0, m_QueryInterface}, {"AddRef", 1, 0, m_AddRef}, {"Release", 1, 0, m_Release}

// ------------------------------------------------------------------------------------------------
// IMMDeviceEnumerator
// ------------------------------------------------------------------------------------------------
static wa_obj *device_new(const wa_endpoint *ep) {
  wa_obj *d = obj_new(K_DEVICE);
  if (d) d->ep = *ep;
  return d;
}

// EnumAudioEndpoints(EDataFlow, DWORD stateMask, IMMDeviceCollection **)
static uint64_t e_EnumAudioEndpoints(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t flow = a[1], mask = a[2], out = a[3];
  if (!out) return E_POINTER;
  if (flow > 2) return E_INVALIDARG;
  wa_obj *l = obj_new(K_COLL);
  if (!l) return E_OUTOFMEMORY;
  l->items = calloc(128, sizeof *l->items);
  l->n_items = (mask & DEVICE_STATE_ACTIVE) && l->items ? wa_endpoints((int)flow, l->items, 128) : 0;
  w32_wr32(out, l->g);
  rs_audio_log("WASAPI: EnumAudioEndpoints(%u, 0x%x): %d endpoints", flow, mask, l->n_items);
  return S_OK;
}

// GetDefaultAudioEndpoint(EDataFlow, ERole, IMMDevice **): every role is the system default
static uint64_t e_GetDefaultAudioEndpoint(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t flow = a[1], out = a[3];
  if (!out) return E_POINTER;
  w32_wr32(out, 0);
  if (flow > 1) return E_INVALIDARG;
  wa_endpoint ep;
  if (!wa_default_endpoint((int)flow, &ep)) return E_NOTFOUND;
  wa_obj *d = device_new(&ep);
  if (!d) return E_OUTOFMEMORY;
  w32_wr32(out, d->g);
  return S_OK;
}

// GetDevice(LPCWSTR id, IMMDevice **)
static uint64_t e_GetDevice(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t out = a[2];
  if (!out || !a[1]) return E_POINTER;
  w32_wr32(out, 0);
  char id[256];
  wstr_get(a[1], id, sizeof id);
  wa_endpoint ep;
  if (!wa_endpoint_by_id(id, &ep)) return E_NOTFOUND;
  wa_obj *d = device_new(&ep);
  if (!d) return E_OUTOFMEMORY;
  w32_wr32(out, d->g);
  return S_OK;
}

// Register/UnregisterEndpointNotificationCallback(IMMNotificationClient *). Wwise's client (vtable
// 0x1ca1da4) acts only on OnDefaultDeviceChanged(eRender, eMultimedia): it sets a byte, and its audio thread
// then makes a new sink on the new default endpoint; the other methods do nothing. So the system's default
// output changing is what is passed on, from a thread of its own (a guest call needs a guest context and
// CoreAudio's threads have none), for each role, as Windows does. The clients are called under the
// lock, so Unregister returns only when its client is out of use.
#define NOTIFY_MAX 8
#define NOTIFY_SLOT_DEFAULT_CHANGED 6
static uint32_t g_notify[NOTIFY_MAX];
static int g_notify_pending, g_notify_started;
static pthread_mutex_t g_notify_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_notify_cv = PTHREAD_COND_INITIALIZER;

void wa_default_changed(void) {
  pthread_mutex_lock(&g_notify_mu);
  g_notify_pending = 1;
  pthread_cond_signal(&g_notify_cv);
  pthread_mutex_unlock(&g_notify_mu);
}
static void *notify_thread(void *arg) {
  (void)arg;
  pthread_setname_np("lp audio notify");
  rc_ctx *c = rc32_thread_ctx();
  uint32_t id = w32_galloc(2 * 256);
  pthread_mutex_lock(&g_notify_mu);
  for (;;) {
    while (!g_notify_pending) pthread_cond_wait(&g_notify_cv, &g_notify_mu);
    g_notify_pending = 0;
    wa_endpoint ep;
    if (!wa_default_endpoint(WA_RENDER, &ep)) continue;
    char s[256];
    wa_endpoint_id(&ep, s, sizeof s);
    for (int k = 0; k < 256; k++) {  // ASCII: wa_endpoint_id's shape and a device UID
      ((uint16_t *)RC32_H(id))[k] = (uint8_t)s[k];
      if (!s[k]) break;
    }
    fprintf(stderr, "audio: the default output is now \"%s\"\n", ep.name);
    for (int i = 0; i < NOTIFY_MAX; i++)
      for (uint32_t role = 0; g_notify[i] && role < 3; role++) {  // eConsole, eMultimedia, eCommunications
        uint32_t a[4] = {g_notify[i], 0, role, id};  // flow 0: eRender
        rc32_call(c, w32_rd32(w32_rd32(g_notify[i]) + 4 * NOTIFY_SLOT_DEFAULT_CHANGED), a, 4, 0, 0, 0);
      }
  }
  return 0;
}
static uint64_t e_RegisterNotify(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1]) return E_POINTER;
  pthread_mutex_lock(&g_notify_mu);
  for (int i = 0; i < NOTIFY_MAX; i++)
    if (!g_notify[i]) { g_notify[i] = a[1]; break; }
  if (!g_notify_started) {
    pthread_t t;
    g_notify_started = !pthread_create(&t, 0, notify_thread, 0);
    if (g_notify_started) pthread_detach(t);
    wa_watch_default_output(wa_default_changed);
  }
  pthread_mutex_unlock(&g_notify_mu);
  return S_OK;
}
static uint64_t e_UnregisterNotify(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint64_t r = E_NOTFOUND;
  pthread_mutex_lock(&g_notify_mu);
  for (int i = 0; i < NOTIFY_MAX; i++)
    if (g_notify[i] == a[1]) {
      g_notify[i] = 0;
      r = S_OK;
      break;
    }
  pthread_mutex_unlock(&g_notify_mu);
  return r;
}

static const w32_slot k_enum_vt[] = {
    IUNKNOWN,
    {"IMMDeviceEnumerator::EnumAudioEndpoints", 4, 0, e_EnumAudioEndpoints},
    {"IMMDeviceEnumerator::GetDefaultAudioEndpoint", 4, 0, e_GetDefaultAudioEndpoint},
    {"IMMDeviceEnumerator::GetDevice", 3, 0, e_GetDevice},
    {"IMMDeviceEnumerator::RegisterEndpointNotificationCallback", 2, 0, e_RegisterNotify},
    {"IMMDeviceEnumerator::UnregisterEndpointNotificationCallback", 2, 0, e_UnregisterNotify},
};

// ------------------------------------------------------------------------------------------------
// IMMDeviceCollection
// ------------------------------------------------------------------------------------------------
static uint64_t l_GetCount(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *o = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], o ? (uint32_t)o->n_items : 0);
  return S_OK;
}
static uint64_t l_Item(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *o = obj_of(a[0]);
  if (!a[2]) return E_POINTER;
  w32_wr32(a[2], 0);
  if (!o || a[1] >= (uint32_t)o->n_items) return E_INVALIDARG;
  wa_obj *d = device_new(&o->items[a[1]]);
  if (!d) return E_OUTOFMEMORY;
  w32_wr32(a[2], d->g);
  return S_OK;
}
static const w32_slot k_coll_vt[] = {
    IUNKNOWN,
    {"IMMDeviceCollection::GetCount", 2, 0, l_GetCount},
    {"IMMDeviceCollection::Item", 3, 0, l_Item},
};

// ------------------------------------------------------------------------------------------------
// IMMDevice, IMMEndpoint
// ------------------------------------------------------------------------------------------------
static wa_obj *client_new(wa_obj *dev);

// Activate(REFIID, DWORD clsctx, PROPVARIANT *params, void **)
static uint64_t d_Activate(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *d = obj_of(a[0]);
  uint32_t iid = a[1], out = a[4];
  if (!out) return E_POINTER;
  w32_wr32(out, 0);
  if (!d) return E_INVALIDARG;
  if (guid_eq(iid, IID_IAudioClient)) {
    wa_obj *cl = client_new(d);
    if (!cl) return E_OUTOFMEMORY;
    w32_wr32(out, cl->g);
    return S_OK;
  }
  if (guid_eq(iid, IID_IAudioEndpointVolume)) {
    wa_obj *v = obj_new(K_VOLUME);
    if (!v) return E_OUTOFMEMORY;
    v->ep = d->ep;
    v->vol = volume_of(&d->ep);
    w32_wr32(out, v->g);
    return S_OK;
  }
  uint32_t d1 = iid ? w32_rd32(iid) : 0;
  rs_audio_log("WASAPI: IMMDevice::Activate: interface {%08x-...} not served", d1);
  return E_NOINTERFACE;
}

// OpenPropertyStore(DWORD stgmAccess, IPropertyStore **)
static uint64_t d_OpenPropertyStore(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *d = obj_of(a[0]);
  if (!a[2]) return E_POINTER;
  w32_wr32(a[2], 0);
  if (!d) return E_INVALIDARG;
  wa_obj *p = obj_new(K_PROPS);
  if (!p) return E_OUTOFMEMORY;
  p->ep = d->ep;
  w32_wr32(a[2], p->g);
  return S_OK;
}

// GetId(LPWSTR *): CoTaskMem string
static uint64_t d_GetId(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *d = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  char id[256];
  if (!d) return E_INVALIDARG;
  wa_endpoint_id(&d->ep, id, sizeof id);
  uint32_t s = wstr_new(id);
  w32_wr32(a[1], s);
  return s ? S_OK : E_OUTOFMEMORY;
}

static uint64_t d_GetState(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], DEVICE_STATE_ACTIVE);
  return S_OK;
}

static const w32_slot k_device_vt[] = {
    IUNKNOWN,
    {"IMMDevice::Activate", 5, 0, d_Activate},
    {"IMMDevice::OpenPropertyStore", 3, 0, d_OpenPropertyStore},
    {"IMMDevice::GetId", 2, 0, d_GetId},
    {"IMMDevice::GetState", 2, 0, d_GetState},
};

static uint64_t n_GetDataFlow(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *e = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], e && e->ep.flow == WA_CAPTURE ? 1 : 0);
  return S_OK;
}
static const w32_slot k_endpoint_vt[] = {
    IUNKNOWN,
    {"IMMEndpoint::GetDataFlow", 2, 0, n_GetDataFlow},
};

// ------------------------------------------------------------------------------------------------
// IPropertyStore (read-only)
// ------------------------------------------------------------------------------------------------
typedef struct {
  const uint8_t *fmtid;
  uint32_t pid;
} pkey;
static const pkey k_keys[] = {
    {FMTID_Device, 14},          // PKEY_Device_FriendlyName
    {FMTID_Device, 2},           // PKEY_Device_DeviceDesc
    {FMTID_DeviceInterface, 2},  // PKEY_DeviceInterface_FriendlyName
    {FMTID_AudioEngine, 0},      // PKEY_AudioEngine_DeviceFormat
    {FMTID_AudioEndpoint, 0},    // PKEY_AudioEndpoint_FormFactor
    {FMTID_EndpointInstance, 2}, // the endpoint's device instance id
};
#define N_KEYS (int)(sizeof k_keys / sizeof k_keys[0])

static uint64_t p_GetCount(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], N_KEYS);
  return S_OK;
}

// GetAt(DWORD, PROPERTYKEY *)
static uint64_t p_GetAt(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[2]) return E_POINTER;
  if (a[1] >= (uint32_t)N_KEYS) return E_INVALIDARG;
  memcpy(RC32_H(a[2]), k_keys[a[1]].fmtid, 16);
  w32_wr32(a[2] + 16, k_keys[a[1]].pid);
  return S_OK;
}

static void pv_set(uint32_t pv, uint16_t vt) {
  memset(RC32_H(pv), 0, 16);
  memcpy(RC32_H(pv), &vt, 2);
}

// GetValue(REFPROPERTYKEY, PROPVARIANT *): unknown keys give VT_EMPTY and S_OK, as Windows does
static uint64_t p_GetValue(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *p = obj_of(a[0]);
  uint32_t key = a[1], pv = a[2];
  if (!key || !pv) return E_POINTER;
  pv_set(pv, VT_EMPTY);
  if (!p) return E_INVALIDARG;
  uint32_t pid = w32_rd32(key + 16);
  const wa_endpoint *ep = &p->ep;
  if ((guid_eq(key, FMTID_Device) && (pid == 14 || pid == 2)) || (guid_eq(key, FMTID_DeviceInterface) && pid == 2)) {
    pv_set(pv, VT_LPWSTR);
    w32_wr32(pv + 8, wstr_new(ep->name));
  } else if (guid_eq(key, FMTID_AudioEngine) && pid == 0) {
    wa_fmt f;
    wa_fmt_mix(ep->channels, &f);
    uint32_t blob = wa_taskmem_alloc(40);
    if (!blob) return E_OUTOFMEMORY;
    wa_fmt_write(&f, RC32_H(blob));
    pv_set(pv, VT_BLOB);
    w32_wr32(pv + 8, 40);
    w32_wr32(pv + 12, blob);
  } else if (guid_eq(key, FMTID_AudioEndpoint) && pid == 0) {
    pv_set(pv, VT_UI4);
    w32_wr32(pv + 8, ep->flow == WA_CAPTURE ? 4 : 1);  // Microphone / Speakers
  } else if (guid_eq(key, FMTID_EndpointInstance) && pid == 2) {
    char inst[96];
    if (ep->vid) snprintf(inst, sizeof inst, "{1}.USB\\VID_%04X&PID_%04X&MI_00\\RSREARMED", ep->vid, ep->pid);
    else snprintf(inst, sizeof inst, "{1}.ROOT\\MEDIA\\%04u", (unsigned)ep->dev);
    pv_set(pv, VT_LPWSTR);
    w32_wr32(pv + 8, wstr_new(inst));
  }
  return S_OK;
}

static uint64_t p_SetValue(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self; (void)a;
  return 0x80030005u;  // STG_E_ACCESSDENIED: opened read-only
}
static uint64_t p_Commit(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self; (void)a;
  return S_OK;
}

static const w32_slot k_props_vt[] = {
    IUNKNOWN,
    {"IPropertyStore::GetCount", 2, 0, p_GetCount},
    {"IPropertyStore::GetAt", 3, 0, p_GetAt},
    {"IPropertyStore::GetValue", 3, 0, p_GetValue},
    {"IPropertyStore::SetValue", 3, 0, p_SetValue},
    {"IPropertyStore::Commit", 1, 0, p_Commit},
};

// ------------------------------------------------------------------------------------------------
// IAudioClient
// ------------------------------------------------------------------------------------------------
#define HNS 10000000ull

// The device period the endpoints report: one I/O buffer of the Mac policy (RS_AUDIO_FRAMES,
// default 256 frames = 5.33 ms at 48 kHz) and half that as the minimum. PortAudio takes the
// default period as the low latency, so a stream gets a 256-frame buffer like the Mac sink's.
static uint32_t period_frames_default(void) {
  uint32_t f = rs_audio_frames_wanted(RS_AUDIO_DEFAULT_FRAMES);
  if (!f) f = RS_AUDIO_DEFAULT_FRAMES;
  if (f < RS_AUDIO_MIN_FRAMES) f = RS_AUDIO_MIN_FRAMES;
  return f;
}
static uint64_t frames_to_hns(uint64_t frames, uint32_t rate) { return (frames * HNS + rate / 2) / rate; }
static uint32_t hns_to_frames(uint64_t hns, uint32_t rate) { return (uint32_t)((hns * rate + HNS / 2) / HNS); }

static wa_obj *client_new(wa_obj *dev) {
  wa_obj *cl = obj_new(K_CLIENT);
  if (!cl) return NULL;
  cl->ep = dev->ep;
  cl->vol = volume_of(&dev->ep);
  return cl;
}

// Formats a stream can be opened with. Shared mode wants the mix rate (48 kHz) and gets the mix
// format as the closest match otherwise; exclusive mode takes what the HAL unit can convert.
static uint32_t check_format(wa_obj *cl, uint32_t share, uint32_t pfmt, uint32_t closest, wa_fmt *out) {
  wa_fmt f;
  if (closest) w32_wr32(closest, 0);
  if (!pfmt) return E_POINTER;
  if (!wa_fmt_parse(RC32_H(pfmt), &f)) return AUDCLNT_E_UNSUPPORTED_FORMAT;
  if (share == 0) {
    wa_fmt mix;
    wa_fmt_mix(cl->ep.channels, &mix);
    if (f.rate != mix.rate) {
      if (closest) {
        uint32_t g = wa_taskmem_alloc(40);
        if (g) {
          mix.channels = f.channels;
          mix.block = (uint16_t)(4 * f.channels);
          mix.mask = 0;
          wa_fmt_write(&mix, RC32_H(g));
          w32_wr32(closest, g);
        }
        return S_FALSE;
      }
      return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
  }
  if (out) *out = f;
  return S_OK;
}

// Initialize(AUDCLNT_SHAREMODE, DWORD flags, REFERENCE_TIME buffer, REFERENCE_TIME period,
//            const WAVEFORMATEX *, LPCGUID session)
static uint64_t c_Initialize(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!cl) return E_INVALIDARG;
  if (cl->initialized) return AUDCLNT_E_ALREADY_INITIALIZED;
  uint32_t share = a[1], flags = a[2], pfmt = a[7];
  uint64_t dur = w32_u64(&a[3]), per = w32_u64(&a[5]);
  if (share > 1) return E_INVALIDARG;
  wa_fmt f;
  uint32_t hr = check_format(cl, share, pfmt, 0, &f);
  if (hr != S_OK) return hr;
  uint32_t dflt = period_frames_default();
  uint32_t buffer = hns_to_frames(dur, f.rate);
  int event = (flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) != 0;
  uint32_t period;
  if (share == 1) {
    // exclusive: the buffer is one period (event mode) or the periodicity sets it
    if (!buffer) buffer = per ? hns_to_frames(per, f.rate) : dflt;
    period = event ? buffer : (per ? hns_to_frames(per, f.rate) : buffer / 2);
  } else {
    if (buffer < 2 * dflt) buffer = 2 * dflt;  // the engine wants at least two device periods
    period = dflt;
  }
  if (buffer < RS_AUDIO_MIN_FRAMES) buffer = RS_AUDIO_MIN_FRAMES;
  if (!period || period > buffer) period = buffer;
  wa_stream *st = wa_stream_open(&cl->ep, &f, buffer, period, share == 1 && event);
  if (!st) return AUDCLNT_E_DEVICE_INVALIDATED;
  cl->scratch = w32_galloc(buffer * f.block);
  if (!cl->scratch) {
    wa_stream_close(st);
    return E_OUTOFMEMORY;
  }
  cl->scratch_frames = buffer;
  cl->st = st;
  cl->fmt = f;
  cl->exclusive = share == 1;
  cl->event_mode = event;
  cl->flags = flags;
  cl->buffer_frames = buffer;
  cl->period_frames = period;
  cl->initialized = 1;
  wa_stream_set_gain(st, volume_gain(cl->vol));
  rs_audio_log("WASAPI: Initialize(%s, 0x%x, %llu, %llu): %u Hz x %u, buffer %u, period %u", share ? "exclusive" : "shared",
               flags, (unsigned long long)dur, (unsigned long long)per, f.rate, f.channels, buffer, period);
  return S_OK;
}

#define NEED_INIT(cl)                                \
  do {                                               \
    if (!(cl)) return E_INVALIDARG;                  \
    if (!(cl)->initialized) return AUDCLNT_E_NOT_INITIALIZED; \
  } while (0)

static uint64_t c_GetBufferSize(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  NEED_INIT(cl);
  w32_wr32(a[1], cl->buffer_frames);
  return S_OK;
}

static uint64_t c_GetStreamLatency(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  NEED_INIT(cl);
  wa_stream_info i;
  wa_stream_info_get(cl->st, &i);
  w32_wr64(a[1], frames_to_hns(i.latency_frames, cl->fmt.rate));
  return S_OK;
}

static uint64_t c_GetCurrentPadding(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  NEED_INIT(cl);
  uint32_t v = cl->ep.flow == WA_CAPTURE ? wa_capture_available(cl->st, 0) : wa_render_padding(cl->st);
  if (v > cl->buffer_frames) v = cl->buffer_frames;
  w32_wr32(a[1], v);
  return S_OK;
}

// IsFormatSupported(AUDCLNT_SHAREMODE, const WAVEFORMATEX *, WAVEFORMATEX **closest)
static uint64_t c_IsFormatSupported(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!cl) return E_INVALIDARG;
  if (a[1] > 1) return E_INVALIDARG;
  return check_format(cl, a[1], a[2], a[1] == 0 ? a[3] : 0, NULL);
}

static uint64_t c_GetMixFormat(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  if (!cl) return E_INVALIDARG;
  wa_fmt f;
  wa_fmt_mix(cl->ep.channels, &f);
  uint32_t g = wa_taskmem_alloc(40);
  if (!g) return E_OUTOFMEMORY;
  wa_fmt_write(&f, RC32_H(g));
  w32_wr32(a[1], g);
  return S_OK;
}

// GetDevicePeriod(REFERENCE_TIME *default, REFERENCE_TIME *minimum): the periods are frames of the
// stream, so they are stated at its rate and a guest turning them back into frames gets the same
// frames. Before Initialize that is the mix rate, not the device's nominal rate: opening a stream
// may still switch the device (use_rate in wa_engine.c).
static uint64_t c_GetDevicePeriod(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!cl) return E_INVALIDARG;
  wa_fmt f = cl->fmt;
  if (!cl->initialized) wa_fmt_mix(cl->ep.channels, &f);
  uint32_t d = period_frames_default(), m = d / 2 < RS_AUDIO_MIN_FRAMES ? RS_AUDIO_MIN_FRAMES : d / 2;
  if (a[1]) w32_wr64(a[1], frames_to_hns(d, f.rate));
  if (a[2]) w32_wr64(a[2], frames_to_hns(m, f.rate));
  return a[1] || a[2] ? S_OK : E_POINTER;
}

static uint64_t c_Start(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  NEED_INIT(cl);
  if (wa_stream_running(cl->st)) return AUDCLNT_E_NOT_STOPPED;
  if (cl->event_mode && !cl->event) return AUDCLNT_E_EVENTHANDLE_NOT_SET;
  return wa_stream_start(cl->st) ? AUDCLNT_E_DEVICE_INVALIDATED : S_OK;
}

static uint64_t c_Stop(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  NEED_INIT(cl);
  if (!wa_stream_running(cl->st)) return S_FALSE;
  wa_stream_stop(cl->st);
  rt_leave(cl);
  return S_OK;
}

static uint64_t c_Reset(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  NEED_INIT(cl);
  if (wa_stream_running(cl->st)) return AUDCLNT_E_NOT_STOPPED;
  wa_stream_reset(cl->st);
  return S_OK;
}

static uint64_t c_SetEventHandle(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  NEED_INIT(cl);
  if (!cl->event_mode) return AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
  if (!a[1]) return E_INVALIDARG;
  cl->event = a[1];
  wa_stream_set_event(cl->st, a[1]);
  return S_OK;
}

// GetService(REFIID, void **)
static uint64_t c_GetService(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *cl = obj_of(a[0]);
  if (!a[2]) return E_POINTER;
  w32_wr32(a[2], 0);
  NEED_INIT(cl);
  int kind = -1;
  if (guid_eq(a[1], IID_IAudioRenderClient)) kind = K_RENDER;
  else if (guid_eq(a[1], IID_IAudioCaptureClient)) kind = K_CAPTURE;
  if (kind < 0) return E_NOINTERFACE;
  if ((kind == K_CAPTURE) != (cl->ep.flow == WA_CAPTURE)) return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
  wa_obj *s = obj_new(kind);
  if (!s) return E_OUTOFMEMORY;
  s->parent = cl;
  obj_addref(cl);
  w32_wr32(a[2], s->g);
  return S_OK;
}

static const w32_slot k_client_vt[] = {
    IUNKNOWN,
    {"IAudioClient::Initialize", 9, 0, c_Initialize},  // this, mode, flags, 2 x REFERENCE_TIME, format, session
    {"IAudioClient::GetBufferSize", 2, 0, c_GetBufferSize},
    {"IAudioClient::GetStreamLatency", 2, 0, c_GetStreamLatency},
    {"IAudioClient::GetCurrentPadding", 2, 0, c_GetCurrentPadding},
    {"IAudioClient::IsFormatSupported", 4, 0, c_IsFormatSupported},
    {"IAudioClient::GetMixFormat", 2, 0, c_GetMixFormat},
    {"IAudioClient::GetDevicePeriod", 3, 0, c_GetDevicePeriod},
    {"IAudioClient::Start", 1, 0, c_Start},
    {"IAudioClient::Stop", 1, 0, c_Stop},
    {"IAudioClient::Reset", 1, 0, c_Reset},
    {"IAudioClient::SetEventHandle", 2, 0, c_SetEventHandle},
    {"IAudioClient::GetService", 3, 0, c_GetService},
};

// ------------------------------------------------------------------------------------------------
// IAudioRenderClient
// ------------------------------------------------------------------------------------------------
// GetBuffer(UINT32 frames, BYTE **data)
static uint64_t r_GetBuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *s = obj_of(a[0]);
  if (!a[2]) return E_POINTER;
  w32_wr32(a[2], 0);
  if (!s || !s->parent) return E_INVALIDARG;
  wa_obj *cl = s->parent;
  uint32_t n = a[1];
  if (cl->pending) return AUDCLNT_E_OUT_OF_ORDER;
  if (n > cl->buffer_frames) return AUDCLNT_E_BUFFER_TOO_LARGE;
  // shared mode: no more than the free part of the buffer; exclusive event mode always gets a
  // period (the ring drops the oldest frames if the guest runs ahead)
  if (!cl->exclusive && n > cl->buffer_frames - wa_render_padding(cl->st)) return AUDCLNT_E_BUFFER_TOO_LARGE;
  if (wa_stream_running(cl->st)) rt_enter(cl);
  wa_stream_set_gain(cl->st, volume_gain(cl->vol));
  cl->pending = n ? n : 0;
  w32_wr32(a[2], cl->scratch);
  return S_OK;
}

// ReleaseBuffer(UINT32 frames, DWORD flags)
static uint64_t r_ReleaseBuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *s = obj_of(a[0]);
  if (!s || !s->parent) return E_INVALIDARG;
  wa_obj *cl = s->parent;
  uint32_t n = a[1], pending = cl->pending;
  cl->pending = 0;
  if (n > pending) return pending ? AUDCLNT_E_INVALID_SIZE : AUDCLNT_E_OUT_OF_ORDER;
  if (n) wa_render_write(cl->st, (a[2] & AUDCLNT_BUFFERFLAGS_SILENT) ? NULL : RC32_H(cl->scratch), n);
  return S_OK;
}

static const w32_slot k_render_vt[] = {
    IUNKNOWN,
    {"IAudioRenderClient::GetBuffer", 3, 0, r_GetBuffer},
    {"IAudioRenderClient::ReleaseBuffer", 3, 0, r_ReleaseBuffer},
};

// ------------------------------------------------------------------------------------------------
// IAudioCaptureClient
// ------------------------------------------------------------------------------------------------
static uint32_t packet_frames(wa_obj *cl, int trim) {
  uint32_t avail = wa_capture_available(cl->st, trim);
  uint32_t p = cl->period_frames;
  return avail >= p ? p : 0;
}

// GetBuffer(BYTE **data, UINT32 *frames, DWORD *flags, UINT64 *devpos, UINT64 *qpcpos)
static uint64_t k_GetBuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *s = obj_of(a[0]);
  if (!a[1] || !a[2] || !a[3]) return E_POINTER;
  w32_wr32(a[1], 0);
  w32_wr32(a[2], 0);
  w32_wr32(a[3], 0);
  if (!s || !s->parent) return E_INVALIDARG;
  wa_obj *cl = s->parent;
  if (cl->pending) return AUDCLNT_E_OUT_OF_ORDER;
  if (wa_stream_running(cl->st)) rt_enter(cl);
  wa_stream_set_gain(cl->st, volume_gain(cl->vol));
  static int trim = -1;  // read once: this runs for every packet
  if (trim < 0) trim = rs_audio_env_on("RS_AUDIO_INPUT_TRIM", 1);
  uint32_t n = packet_frames(cl, trim);
  if (a[4]) w32_wr64(a[4], wa_capture_position(cl->st));
  if (a[5]) w32_wr64(a[5], 0);
  if (!n) return AUDCLNT_S_BUFFER_EMPTY;
  n = wa_capture_peek(cl->st, RC32_H(cl->scratch), n);
  uint32_t ov = wa_capture_overruns(cl->st), flags = 0;
  if (ov != cl->overruns_seen) {
    flags |= AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY;
    cl->overruns_seen = ov;
  }
  cl->pending = n;
  w32_wr32(a[1], cl->scratch);
  w32_wr32(a[2], n);
  w32_wr32(a[3], flags);
  return S_OK;
}

// ReleaseBuffer(UINT32 frames): all of the packet or none of it
static uint64_t k_ReleaseBuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *s = obj_of(a[0]);
  if (!s || !s->parent) return E_INVALIDARG;
  wa_obj *cl = s->parent;
  uint32_t n = a[1], pending = cl->pending;
  cl->pending = 0;
  if (n && n != pending) return AUDCLNT_E_INVALID_SIZE;
  if (n) wa_capture_consume(cl->st, n);
  return S_OK;
}

static uint64_t k_GetNextPacketSize(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *s = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  if (!s || !s->parent) return E_INVALIDARG;
  w32_wr32(a[1], packet_frames(s->parent, 0));
  return S_OK;
}

static const w32_slot k_capture_vt[] = {
    IUNKNOWN,
    {"IAudioCaptureClient::GetBuffer", 6, 0, k_GetBuffer},
    {"IAudioCaptureClient::ReleaseBuffer", 2, 0, k_ReleaseBuffer},
    {"IAudioCaptureClient::GetNextPacketSize", 2, 0, k_GetNextPacketSize},
};

// ------------------------------------------------------------------------------------------------
// IAudioEndpointVolume: a software gain on the endpoint's streams (the device's own volume, a
// system setting, is left alone)
// ------------------------------------------------------------------------------------------------
#define VOL_MIN_DB -96.f

static float f32(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
static void wrf(uint32_t g, float f) { memcpy(RC32_H(g), &f, 4); }
static float db_to_scalar(float db) { return db <= VOL_MIN_DB ? 0.f : powf(10.f, db / 20.f); }
static float scalar_to_db(float s) { return s <= 0.f ? VOL_MIN_DB : 20.f * log10f(s); }

static uint64_t v_ok(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; (void)a; return S_OK; }
static uint64_t v_GetChannelCount(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *v = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], v ? v->ep.channels : 1);
  return S_OK;
}
static void vol_set(wa_obj *v, float s) {
  if (!v || !v->vol) return;
  if (s < 0.f) s = 0.f;
  if (s > 1.f) s = 1.f;
  atomic_store(&v->vol->scalar, s);
}
static float vol_get(wa_obj *v) { return v && v->vol ? atomic_load(&v->vol->scalar) : 1.f; }
// SetMasterVolumeLevel(float dB, LPCGUID) / SetMasterVolumeLevelScalar(float, LPCGUID)
static uint64_t v_SetLevel(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; vol_set(obj_of(a[0]), db_to_scalar(f32(a[1]))); return S_OK; }
static uint64_t v_SetScalar(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; vol_set(obj_of(a[0]), f32(a[1])); return S_OK; }
static uint64_t v_GetLevel(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1]) return E_POINTER;
  wrf(a[1], scalar_to_db(vol_get(obj_of(a[0]))));
  return S_OK;
}
static uint64_t v_GetScalar(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1]) return E_POINTER;
  wrf(a[1], vol_get(obj_of(a[0])));
  return S_OK;
}
// the channel versions act on the master gain (channel, value, context)
static uint64_t v_SetChLevel(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; vol_set(obj_of(a[0]), db_to_scalar(f32(a[2]))); return S_OK; }
static uint64_t v_SetChScalar(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; vol_set(obj_of(a[0]), f32(a[2])); return S_OK; }
static uint64_t v_GetChLevel(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[2]) return E_POINTER;
  wrf(a[2], scalar_to_db(vol_get(obj_of(a[0]))));
  return S_OK;
}
static uint64_t v_GetChScalar(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[2]) return E_POINTER;
  wrf(a[2], vol_get(obj_of(a[0])));
  return S_OK;
}
static uint64_t v_SetMute(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *v = obj_of(a[0]);
  if (v && v->vol) atomic_store(&v->vol->mute, a[1] != 0);
  return S_OK;
}
static uint64_t v_GetMute(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wa_obj *v = obj_of(a[0]);
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], v && v->vol ? (uint32_t)atomic_load(&v->vol->mute) : 0);
  return S_OK;
}
static uint64_t v_GetVolumeStepInfo(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  out32(a[1], (uint32_t)(lrintf(vol_get(obj_of(a[0])) * 100.f)));
  out32(a[2], 101);
  return S_OK;
}
static uint64_t v_Step(rc_ctx *c, uint32_t self, const uint32_t *a, float d) {
  (void)c; (void)self;
  wa_obj *v = obj_of(a[0]);
  vol_set(v, vol_get(v) + d);
  return S_OK;
}
static uint64_t v_StepUp(rc_ctx *c, uint32_t self, const uint32_t *a) { return v_Step(c, self, a, 0.01f); }
static uint64_t v_StepDown(rc_ctx *c, uint32_t self, const uint32_t *a) { return v_Step(c, self, a, -0.01f); }
static uint64_t v_QueryHardwareSupport(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1]) return E_POINTER;
  w32_wr32(a[1], 0);  // all in software
  return S_OK;
}
// GetVolumeRange(float *min_dB, float *max_dB, float *step_dB)
static uint64_t v_GetVolumeRange(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[1] || !a[2] || !a[3]) return E_POINTER;
  wrf(a[1], VOL_MIN_DB);
  wrf(a[2], 0.f);
  wrf(a[3], 0.5f);
  return S_OK;
}

static const w32_slot k_volume_vt[] = {
    IUNKNOWN,
    {"IAudioEndpointVolume::RegisterControlChangeNotify", 2, 0, v_ok},
    {"IAudioEndpointVolume::UnregisterControlChangeNotify", 2, 0, v_ok},
    {"IAudioEndpointVolume::GetChannelCount", 2, 0, v_GetChannelCount},
    {"IAudioEndpointVolume::SetMasterVolumeLevel", 3, 0, v_SetLevel},
    {"IAudioEndpointVolume::SetMasterVolumeLevelScalar", 3, 0, v_SetScalar},
    {"IAudioEndpointVolume::GetMasterVolumeLevel", 2, 0, v_GetLevel},
    {"IAudioEndpointVolume::GetMasterVolumeLevelScalar", 2, 0, v_GetScalar},
    {"IAudioEndpointVolume::SetChannelVolumeLevel", 4, 0, v_SetChLevel},
    {"IAudioEndpointVolume::SetChannelVolumeLevelScalar", 4, 0, v_SetChScalar},
    {"IAudioEndpointVolume::GetChannelVolumeLevel", 3, 0, v_GetChLevel},
    {"IAudioEndpointVolume::GetChannelVolumeLevelScalar", 3, 0, v_GetChScalar},
    {"IAudioEndpointVolume::SetMute", 3, 0, v_SetMute},
    {"IAudioEndpointVolume::GetMute", 2, 0, v_GetMute},
    {"IAudioEndpointVolume::GetVolumeStepInfo", 3, 0, v_GetVolumeStepInfo},
    {"IAudioEndpointVolume::VolumeStepUp", 2, 0, v_StepUp},
    {"IAudioEndpointVolume::VolumeStepDown", 2, 0, v_StepDown},
    {"IAudioEndpointVolume::QueryHardwareSupport", 2, 0, v_QueryHardwareSupport},
    {"IAudioEndpointVolume::GetVolumeRange", 4, 0, v_GetVolumeRange},
};

// ------------------------------------------------------------------------------------------------
// Vtables, interface ids, CoCreateInstance
// ------------------------------------------------------------------------------------------------
#define VT(t) w32_vtable(t, (int)(sizeof t / sizeof t[0]), W32_STDCALL, E_NOTIMPL)

static uint32_t vtable_of(int kind) {
  switch (kind) {
  case K_ENUM: return VT(k_enum_vt);
  case K_COLL: return VT(k_coll_vt);
  case K_DEVICE: return VT(k_device_vt);
  case K_ENDPOINT: return VT(k_endpoint_vt);
  case K_PROPS: return VT(k_props_vt);
  case K_CLIENT: return VT(k_client_vt);
  case K_RENDER: return VT(k_render_vt);
  case K_CAPTURE: return VT(k_capture_vt);
  case K_VOLUME: return VT(k_volume_vt);
  }
  return 0;
}

static uint32_t iid_ok(wa_obj *o, uint32_t iid) {
  if (guid_eq(iid, IID_IUnknown)) return 1;
  switch (o->kind) {
  case K_ENUM: return guid_eq(iid, IID_IMMDeviceEnumerator);
  case K_COLL: return guid_eq(iid, IID_IMMDeviceCollection);
  case K_DEVICE: return guid_eq(iid, IID_IMMDevice);
  case K_ENDPOINT: return guid_eq(iid, IID_IMMEndpoint);
  case K_PROPS: return guid_eq(iid, IID_IPropertyStore);
  case K_CLIENT: return guid_eq(iid, IID_IAudioClient);
  case K_RENDER: return guid_eq(iid, IID_IAudioRenderClient);
  case K_CAPTURE: return guid_eq(iid, IID_IAudioCaptureClient);
  case K_VOLUME: return guid_eq(iid, IID_IAudioEndpointVolume);
  }
  return 0;
}

int wasapi_has_class(uint32_t clsid) { return guid_eq(clsid, CLSID_MMDeviceEnumerator); }

uint32_t wasapi_cocreate(uint32_t clsid, uint32_t iid, uint32_t guest_out) {
  if (!guest_out) return E_POINTER;
  w32_wr32(guest_out, 0);
  if (!wasapi_has_class(clsid)) return REGDB_E_CLASSNOTREG;
  if (!guid_eq(iid, IID_IMMDeviceEnumerator) && !guid_eq(iid, IID_IUnknown)) return E_NOINTERFACE;
  wa_obj *e = obj_new(K_ENUM);
  if (!e) return E_OUTOFMEMORY;
  w32_wr32(guest_out, e->g);
  rs_audio_log("WASAPI: MMDeviceEnumerator created");
  return S_OK;
}
