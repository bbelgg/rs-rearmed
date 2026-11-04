// winmm face: waveIn and the mixer for the guitar input, on the capture streams of wa_engine.c.
//
// NSoundInputMgr_Win32 uses waveIn when PortAudio can't start (or GR.Audio.ForceUseWaveIn is
// set): waveInGetNumDevs / waveInGetDevCapsA to find a device whose name holds "Rocksmith",
// waveInOpen with CALLBACK_FUNCTION (48 kHz mono, 32-bit float or 16-bit PCM), 24 headers of 128
// frames, waveInStart / Stop / Close; the mixer's volume control of that device. Devices are the
// CoreAudio inputs, the system default first; the Real Tone cable's CoreAudio name is the one
// Windows gives it, "Rocksmith USB Guitar Adapter".
//
// Buffers are filled on a worker thread per open device, woken by the device's I/O thread, and
// returned to the guest there (CALLBACK_FUNCTION runs on that thread with its own guest context,
// as Windows runs it on a driver thread; CALLBACK_EVENT sets the event). With no buffer queued,
// incoming audio is dropped, as on Windows, so nothing stale is ever handed out late.
#include <ctype.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "audio.h"
#include "rs_audio_host.h"
#include "wa_engine.h"
#include "win32_guestobj.h"
#include "win32_shims.h"

#define MMSYSERR_NOERROR 0u
#define MMSYSERR_ERROR 1u
#define MMSYSERR_BADDEVICEID 2u
#define MMSYSERR_INVALHANDLE 5u
#define MMSYSERR_NOMEM 7u
#define MMSYSERR_NOTSUPPORTED 8u
#define MMSYSERR_INVALFLAG 10u
#define MMSYSERR_INVALPARAM 11u
#define WAVERR_BADFORMAT 32u
#define WAVERR_STILLPLAYING 33u
#define WAVERR_UNPREPARED 34u
#define MIXERR_INVALLINE 1024u
#define MIXERR_INVALCONTROL 1025u
#define WAVE_MAPPER 0xFFFFFFFFu
#define WAVE_FORMAT_QUERY 1u
#define CALLBACK_TYPEMASK 0x70000u
#define CALLBACK_NULL 0u
#define CALLBACK_FUNCTION 0x30000u
#define CALLBACK_EVENT 0x50000u
#define WIM_OPEN 0x3BEu
#define WIM_CLOSE 0x3BFu
#define WIM_DATA 0x3C0u
#define WHDR_DONE 1u
#define WHDR_PREPARED 2u
#define WHDR_INQUEUE 0x10u
#define MM_MICROSOFT 1
#define MM_WDMAUDIO_WAVEIN 101

#define MAX_IN 32
#define MAX_HDRS 128
#define WAVEIN_TAG 0x7a000000u
#define MIXER_TAG 0x7b000000u

// WAVEHDR: lpData, dwBufferLength, dwBytesRecorded, dwUser, dwFlags, dwLoops, lpNext, reserved
#define HDR_DATA 0
#define HDR_LEN 4
#define HDR_REC 8
#define HDR_FLAGS 16

// ------------------------------------------------------------------------------------------------
// Devices
// ------------------------------------------------------------------------------------------------
static int in_devices(wa_endpoint *out, int max) {
  int n = wa_endpoints(WA_CAPTURE, out, max);
  wa_endpoint d;
  if (n > 1 && wa_default_endpoint(WA_CAPTURE, &d)) {
    for (int i = 1; i < n; i++)
      if (out[i].dev == d.dev) {
        wa_endpoint t = out[i];
        memmove(out + 1, out, (size_t)i * sizeof *out);
        out[0] = t;
        break;
      }
  }
  return n;
}

static int in_device(uint32_t id, wa_endpoint *ep) {
  wa_endpoint eps[MAX_IN];
  int n = in_devices(eps, MAX_IN);
  if (id == WAVE_MAPPER) id = 0;
  if (id >= (uint32_t)n) return 0;
  *ep = eps[id];
  return 1;
}

// ------------------------------------------------------------------------------------------------
// waveIn
// ------------------------------------------------------------------------------------------------
typedef struct {
  int used;
  uint32_t id;
  wa_endpoint ep;
  wa_fmt fmt;
  wa_stream *st;
  uint32_t cb, inst, mode;
  pthread_mutex_t mu;
  uint32_t q[MAX_HDRS];  // queued WAVEHDRs, oldest first
  int nq;
  dispatch_semaphore_t wake;
  pthread_t worker;
  _Atomic int run_worker, started;
  uint64_t frames;  // captured and handed out
} wave_in;

static wave_in g_in[16];
static pthread_mutex_t g_in_mu = PTHREAD_MUTEX_INITIALIZER;

static wave_in *in_of(uint32_t h) {
  if ((h & 0xff000000u) != WAVEIN_TAG) return NULL;
  uint32_t k = h & 0xffffu;
  return k < 16 && g_in[k].used ? &g_in[k] : NULL;
}

// The guest's callback: on whichever thread runs this (c is that thread's guest context).
static void in_callback(rc_ctx *c, wave_in *w, uint32_t h, uint32_t msg, uint32_t p1) {
  if (w->mode == CALLBACK_FUNCTION && w->cb) {
    uint32_t args[5] = {h, msg, w->inst, p1, 0};
    w32_guest_call(c, w->cb, W32_STDCALL, 0, args, 5, NULL);
  } else if (w->mode == CALLBACK_EVENT && w->cb) {
    w32_event_signal(w->cb);
  }
}

static void hdr_done(rc_ctx *c, wave_in *w, uint32_t h, uint32_t hdr) {
  uint32_t f = w32_rd32(hdr + HDR_FLAGS);
  w32_wr32(hdr + HDR_FLAGS, (f & ~WHDR_INQUEUE) | WHDR_DONE);
  in_callback(c, w, h, WIM_DATA, hdr);
}

static void wake_worker(void *arg) { dispatch_semaphore_signal(((wave_in *)arg)->wake); }

static void *in_worker(void *arg) {
  wave_in *w = arg;
  uint32_t h = WAVEIN_TAG | (uint32_t)(w - g_in);
  rc_ctx *c = rc32_thread_ctx();
  if (rs_audio_env_on("RS_AUDIO_RT", 1)) rs_audio_make_realtime(128, w->fmt.rate, "waveIn thread");
  while (atomic_load(&w->run_worker)) {
    dispatch_semaphore_wait(w->wake, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC));
    wa_stream_set_gain(w->st, wa_endpoint_gain(&w->ep));
    for (;;) {
      pthread_mutex_lock(&w->mu);
      uint32_t avail = wa_capture_available(w->st, 0);
      if (!w->nq) {
        wa_capture_consume(w->st, avail);  // nothing queued: dropped, as on Windows
        pthread_mutex_unlock(&w->mu);
        break;
      }
      if (!avail) {
        pthread_mutex_unlock(&w->mu);
        break;
      }
      uint32_t hdr = w->q[0];
      uint32_t len = w32_rd32(hdr + HDR_LEN), rec = w32_rd32(hdr + HDR_REC);
      uint32_t need = (len - rec) / w->fmt.block;
      uint32_t n = avail < need ? avail : need;
      n = wa_capture_peek(w->st, RC32_H(w32_rd32(hdr + HDR_DATA) + rec), n);
      wa_capture_consume(w->st, n);
      rec += n * w->fmt.block;
      w->frames += n;
      w32_wr32(hdr + HDR_REC, rec);
      int full = (len - rec) < w->fmt.block;
      if (full) {
        w->nq--;
        memmove(w->q, w->q + 1, (size_t)w->nq * sizeof w->q[0]);
      }
      pthread_mutex_unlock(&w->mu);
      if (full) hdr_done(c, w, h, hdr);
    }
  }
  return NULL;
}

static uint64_t wi_GetNumDevs(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self; (void)a;
  wa_endpoint eps[MAX_IN];
  return (uint32_t)in_devices(eps, MAX_IN);
}

static uint32_t id_of(uint32_t idh) {
  wave_in *w = in_of(idh);
  return w ? w->id : idh;
}

// WAVEINCAPSA: wMid, wPid, vDriverVersion, szPname[32], dwFormats, wChannels, wReserved1
static uint64_t devcaps(uint32_t idh, uint32_t caps, uint32_t size, int wide) {
  wa_endpoint ep;
  if (!caps) return MMSYSERR_INVALPARAM;
  if (!in_device(id_of(idh), &ep)) return MMSYSERR_BADDEVICEID;
  uint8_t b[80] = {0};
  uint16_t mid = MM_MICROSOFT, pid = MM_WDMAUDIO_WAVEIN;
  memcpy(b, &mid, 2);
  memcpy(b + 2, &pid, 2);
  uint32_t ver = 0x0a00, formats = 0xfffff;
  memcpy(b + 4, &ver, 4);
  int namelen = wide ? 64 : 32;
  if (wide) {
    for (int i = 0; i < 31 && ep.name[i]; i++) b[8 + 2 * i] = (uint8_t)(ep.name[i] & 0x7f);
  } else {
    snprintf((char *)b + 8, 32, "%s", ep.name);  // 31 characters, as winmm cuts it
  }
  memcpy(b + 8 + namelen, &formats, 4);
  uint16_t ch = ep.channels;
  memcpy(b + 12 + namelen, &ch, 2);
  uint32_t total = 16 + (uint32_t)namelen;
  memcpy(RC32_H(caps), b, size < total ? size : total);
  return MMSYSERR_NOERROR;
}
static uint64_t wi_GetDevCapsA(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; return devcaps(a[0], a[1], a[2], 0); }
static uint64_t wi_GetDevCapsW(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; return devcaps(a[0], a[1], a[2], 1); }

static uint64_t wi_GetID(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wave_in *w = in_of(a[0]);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!a[1]) return MMSYSERR_INVALPARAM;
  w32_wr32(a[1], w->id);
  return MMSYSERR_NOERROR;
}

// waveInOpen(HWAVEIN *, UINT id, const WAVEFORMATEX *, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
static uint64_t wi_Open(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)self;
  uint32_t ph = a[0], id = a[1], pfmt = a[2], cb = a[3], inst = a[4], flags = a[5];
  wa_endpoint ep;
  wa_fmt f;
  if (!pfmt) return MMSYSERR_INVALPARAM;
  if (!in_device(id, &ep)) return MMSYSERR_BADDEVICEID;
  if (!wa_fmt_parse(RC32_H(pfmt), &f)) return WAVERR_BADFORMAT;
  if (flags & WAVE_FORMAT_QUERY) return MMSYSERR_NOERROR;
  uint32_t mode = flags & CALLBACK_TYPEMASK;
  if (mode != CALLBACK_NULL && mode != CALLBACK_FUNCTION && mode != CALLBACK_EVENT) {
    rs_audio_log("waveInOpen: callback type 0x%x not served", mode);
    return MMSYSERR_NOTSUPPORTED;
  }
  if (!ph) return MMSYSERR_INVALPARAM;
  pthread_mutex_lock(&g_in_mu);
  int k = 0;
  while (k < 16 && g_in[k].used) k++;
  if (k == 16) {
    pthread_mutex_unlock(&g_in_mu);
    return MMSYSERR_NOMEM;
  }
  wave_in *w = &g_in[k];
  memset(w, 0, sizeof *w);
  w->used = 1;
  pthread_mutex_unlock(&g_in_mu);
  if (id == WAVE_MAPPER) id = 0;
  w->id = id;
  w->ep = ep;
  w->fmt = f;
  w->cb = cb;
  w->inst = inst;
  w->mode = mode;
  // a quarter second of ring; the worker hands it out as soon as a period is in
  w->st = wa_stream_open(&ep, &f, f.rate / 4, 32, 0);
  if (!w->st) {
    w->used = 0;
    return MMSYSERR_ERROR;
  }
  pthread_mutex_init(&w->mu, NULL);
  w->wake = dispatch_semaphore_create(0);
  wa_stream_set_notify(w->st, wake_worker, w);
  uint32_t h = WAVEIN_TAG | (uint32_t)k;
  w32_wr32(ph, h);
  rs_audio_log("waveInOpen(%u \"%s\", %u Hz x %u %s%u, callback 0x%x)", id, ep.name, f.rate, f.channels,
               f.is_float ? "float" : "int", f.bits, mode);
  in_callback(c, w, h, WIM_OPEN, 0);
  return MMSYSERR_NOERROR;
}

static uint64_t wi_PrepareHeader(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!in_of(a[0])) return MMSYSERR_INVALHANDLE;
  if (!a[1] || a[2] < 32) return MMSYSERR_INVALPARAM;
  w32_wr32(a[1] + HDR_FLAGS, w32_rd32(a[1] + HDR_FLAGS) | WHDR_PREPARED);
  return MMSYSERR_NOERROR;
}

static uint64_t wi_UnprepareHeader(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!in_of(a[0])) return MMSYSERR_INVALHANDLE;
  if (!a[1] || a[2] < 32) return MMSYSERR_INVALPARAM;
  uint32_t f = w32_rd32(a[1] + HDR_FLAGS);
  if (f & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  w32_wr32(a[1] + HDR_FLAGS, f & ~WHDR_PREPARED);
  return MMSYSERR_NOERROR;
}

static uint64_t wi_AddBuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wave_in *w = in_of(a[0]);
  uint32_t hdr = a[1];
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!hdr || a[2] < 32) return MMSYSERR_INVALPARAM;
  uint32_t f = w32_rd32(hdr + HDR_FLAGS);
  if (!(f & WHDR_PREPARED)) return WAVERR_UNPREPARED;
  if (f & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  pthread_mutex_lock(&w->mu);
  if (w->nq == MAX_HDRS) {
    pthread_mutex_unlock(&w->mu);
    return MMSYSERR_NOMEM;
  }
  w32_wr32(hdr + HDR_REC, 0);
  w32_wr32(hdr + HDR_FLAGS, (f & ~WHDR_DONE) | WHDR_INQUEUE);
  w->q[w->nq++] = hdr;
  pthread_mutex_unlock(&w->mu);
  return MMSYSERR_NOERROR;
}

static uint64_t wi_Start(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wave_in *w = in_of(a[0]);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (atomic_load(&w->started)) return MMSYSERR_NOERROR;
  wa_stream_reset(w->st);
  atomic_store(&w->run_worker, 1);
  if (pthread_create(&w->worker, NULL, in_worker, w) != 0) {
    atomic_store(&w->run_worker, 0);
    return MMSYSERR_NOMEM;
  }
  if (wa_stream_start(w->st)) {
    atomic_store(&w->run_worker, 0);
    dispatch_semaphore_signal(w->wake);
    pthread_join(w->worker, NULL);
    return MMSYSERR_ERROR;
  }
  atomic_store(&w->started, 1);
  return MMSYSERR_NOERROR;
}

static void stop_worker(wave_in *w) {
  if (!atomic_load(&w->started)) return;
  wa_stream_stop(w->st);
  atomic_store(&w->run_worker, 0);
  dispatch_semaphore_signal(w->wake);
  pthread_join(w->worker, NULL);
  atomic_store(&w->started, 0);
}

// Stop: the buffer being filled goes back with what it holds; empty ones stay queued.
static uint64_t wi_Stop(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)self;
  wave_in *w = in_of(a[0]);
  if (!w) return MMSYSERR_INVALHANDLE;
  stop_worker(w);
  pthread_mutex_lock(&w->mu);
  uint32_t hdr = 0;
  if (w->nq && w32_rd32(w->q[0] + HDR_REC)) {
    hdr = w->q[0];
    w->nq--;
    memmove(w->q, w->q + 1, (size_t)w->nq * sizeof w->q[0]);
  }
  pthread_mutex_unlock(&w->mu);
  if (hdr) hdr_done(c, w, a[0], hdr);
  return MMSYSERR_NOERROR;
}

// Reset: stop and give back every queued buffer.
static uint64_t wi_Reset(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)self;
  wave_in *w = in_of(a[0]);
  if (!w) return MMSYSERR_INVALHANDLE;
  stop_worker(w);
  for (;;) {
    pthread_mutex_lock(&w->mu);
    uint32_t hdr = 0;
    if (w->nq) {
      hdr = w->q[0];
      w->nq--;
      memmove(w->q, w->q + 1, (size_t)w->nq * sizeof w->q[0]);
    }
    pthread_mutex_unlock(&w->mu);
    if (!hdr) break;
    hdr_done(c, w, a[0], hdr);
  }
  wa_stream_reset(w->st);
  return MMSYSERR_NOERROR;
}

static uint64_t wi_Close(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)self;
  wave_in *w = in_of(a[0]);
  if (!w) return MMSYSERR_INVALHANDLE;
  pthread_mutex_lock(&w->mu);
  int busy = w->nq != 0;
  pthread_mutex_unlock(&w->mu);
  if (busy) return WAVERR_STILLPLAYING;
  stop_worker(w);
  in_callback(c, w, a[0], WIM_CLOSE, 0);
  wa_stream_close(w->st);
  pthread_mutex_destroy(&w->mu);
  dispatch_release(w->wake);
  pthread_mutex_lock(&g_in_mu);
  w->used = 0;
  pthread_mutex_unlock(&g_in_mu);
  return MMSYSERR_NOERROR;
}

// waveInGetPosition(HWAVEIN, MMTIME *, UINT): TIME_BYTES / TIME_SAMPLES
static uint64_t wi_GetPosition(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  wave_in *w = in_of(a[0]);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!a[1] || a[2] < 8) return MMSYSERR_INVALPARAM;
  uint32_t type = w32_rd32(a[1]);
  if (type == 2) {
    w32_wr32(a[1] + 4, (uint32_t)w->frames);
  } else {
    w32_wr32(a[1], 4);
    w32_wr32(a[1] + 4, (uint32_t)(w->frames * w->fmt.block));
  }
  return MMSYSERR_NOERROR;
}

// ------------------------------------------------------------------------------------------------
// Mixer: one per input device, a "Recording Control" destination (WAVEIN) with one microphone
// source; each line has a volume and a mute control, all on the endpoint's software gain
// ------------------------------------------------------------------------------------------------
#define MIXERLINE_COMPONENTTYPE_DST_WAVEIN 7u
#define MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE 0x1003u
#define MIXERLINE_TARGETTYPE_WAVEIN 2u
#define MIXERCONTROL_CONTROLTYPE_VOLUME 0x50030001u
#define MIXERCONTROL_CONTROLTYPE_MUTE 0x20010002u
#define LINE_DST 0xffff0000u
#define LINE_SRC 0x00000000u
#define MIXER_OBJECTF_HANDLE 0x80000000u
#define MIXER_OBJECTF_WAVEIN 0x20000000u
#define MIXER_OBJECTF_TYPEMASK 0xf0000000u

typedef struct {
  int used;
  uint32_t id;
} mixer;
static mixer g_mix[16];

// A mixer id from a handle or id, as the call's MIXER_OBJECTF_* flags say.
static int mixer_id(uint32_t hmx, uint32_t flags, uint32_t *id) {
  uint32_t t = flags & MIXER_OBJECTF_TYPEMASK;
  if (t == (MIXER_OBJECTF_HANDLE | MIXER_OBJECTF_WAVEIN)) {
    wave_in *w = in_of(hmx);
    if (!w) return 0;
    *id = w->id;
  } else if ((hmx & 0xff000000u) == MIXER_TAG) {
    uint32_t k = hmx & 0xffffu;
    if (k >= 16 || !g_mix[k].used) return 0;
    *id = g_mix[k].id;
  } else {
    *id = hmx;  // a mixer or waveIn device id: the same numbers here
  }
  wa_endpoint ep;
  return in_device(*id, &ep);
}

static uint64_t mx_GetNumDevs(rc_ctx *c, uint32_t self, const uint32_t *a) { return wi_GetNumDevs(c, self, a); }

// mixerOpen(HMIXER *, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
static uint64_t mx_Open(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t id;
  if (!a[0]) return MMSYSERR_INVALPARAM;
  if (!mixer_id(a[1], a[4], &id)) return MMSYSERR_BADDEVICEID;
  int k = 0;
  while (k < 16 && g_mix[k].used) k++;
  if (k == 16) return MMSYSERR_NOMEM;
  g_mix[k] = (mixer){1, id};
  w32_wr32(a[0], MIXER_TAG | (uint32_t)k);
  return MMSYSERR_NOERROR;
}

static uint64_t mx_Close(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t k = a[0] & 0xffffu;
  if ((a[0] & 0xff000000u) != MIXER_TAG || k >= 16 || !g_mix[k].used) return MMSYSERR_INVALHANDLE;
  g_mix[k].used = 0;
  return MMSYSERR_NOERROR;
}

// mixerGetID(HMIXEROBJ, UINT *, DWORD flags)
static uint64_t mx_GetID(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t id;
  if (!a[1]) return MMSYSERR_INVALPARAM;
  if (!mixer_id(a[0], a[2], &id)) return MMSYSERR_INVALHANDLE;
  w32_wr32(a[1], id);
  return MMSYSERR_NOERROR;
}

// MIXERCAPSA: wMid, wPid, vDriverVersion, szPname[32], fdwSupport, cDestinations
static uint64_t mx_GetDevCapsA(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t id;
  wa_endpoint ep;
  if (!a[1]) return MMSYSERR_INVALPARAM;
  if (!mixer_id(a[0], 0, &id) || !in_device(id, &ep)) return MMSYSERR_BADDEVICEID;
  uint8_t b[48] = {0};
  uint16_t mid = MM_MICROSOFT, pid = MM_WDMAUDIO_WAVEIN;
  uint32_t ver = 0x0a00, dests = 1;
  memcpy(b, &mid, 2);
  memcpy(b + 2, &pid, 2);
  memcpy(b + 4, &ver, 4);
  snprintf((char *)b + 8, 32, "%s", ep.name);
  memcpy(b + 44, &dests, 4);
  memcpy(RC32_H(a[1]), b, a[2] < 48 ? a[2] : 48);
  return MMSYSERR_NOERROR;
}

// MIXERLINEA (168 bytes)
static void fill_line(uint32_t p, uint32_t id, const wa_endpoint *ep, int src) {
  uint32_t cb = w32_rd32(p);
  uint8_t b[168] = {0};
  uint32_t v;
  memcpy(b, &cb, 4);
  v = 0; memcpy(b + 4, &v, 4);                                      // dwDestination
  v = 0; memcpy(b + 8, &v, 4);                                      // dwSource
  v = src ? LINE_SRC : LINE_DST; memcpy(b + 12, &v, 4);             // dwLineID
  v = src ? 0x80000001u : 1u; memcpy(b + 16, &v, 4);                // fdwLine: ACTIVE (| SOURCE)
  v = src ? MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE : MIXERLINE_COMPONENTTYPE_DST_WAVEIN;
  memcpy(b + 24, &v, 4);
  v = ep->channels > 2 ? 2 : ep->channels; memcpy(b + 28, &v, 4);   // cChannels
  v = src ? 0 : 1; memcpy(b + 32, &v, 4);                           // cConnections
  v = 2; memcpy(b + 36, &v, 4);                                     // cControls
  snprintf((char *)b + 40, 16, "%s", src ? "Microphone" : "Recording");
  snprintf((char *)b + 56, 64, "%s", src ? "Microphone" : "Recording Control");
  v = MIXERLINE_TARGETTYPE_WAVEIN; memcpy(b + 120, &v, 4);
  memcpy(b + 124, &id, 4);
  uint16_t mid = MM_MICROSOFT, pid = MM_WDMAUDIO_WAVEIN;
  memcpy(b + 128, &mid, 2);
  memcpy(b + 130, &pid, 2);
  v = 0x0a00; memcpy(b + 132, &v, 4);
  snprintf((char *)b + 136, 32, "%s", ep->name);
  memcpy(RC32_H(p), b, cb < 168 ? cb : 168);
}

// mixerGetLineInfoA(HMIXEROBJ, MIXERLINEA *, DWORD flags)
static uint64_t mx_GetLineInfoA(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t id, p = a[1], q = a[2] & 0xf;
  wa_endpoint ep;
  if (!p || w32_rd32(p) < 168) return MMSYSERR_INVALPARAM;
  if (!mixer_id(a[0], a[2], &id) || !in_device(id, &ep)) return MMSYSERR_INVALHANDLE;
  int src;
  switch (q) {
  case 0: if (w32_rd32(p + 4) != 0) return MIXERR_INVALLINE; src = 0; break;  // DESTINATION
  case 1: if (w32_rd32(p + 4) != 0 || w32_rd32(p + 8) != 0) return MIXERR_INVALLINE; src = 1; break;  // SOURCE
  case 2: {  // LINEID
    uint32_t l = w32_rd32(p + 12);
    if (l != LINE_DST && l != LINE_SRC) return MIXERR_INVALLINE;
    src = l == LINE_SRC;
    break;
  }
  case 3: {  // COMPONENTTYPE
    uint32_t t = w32_rd32(p + 24);
    if (t == MIXERLINE_COMPONENTTYPE_DST_WAVEIN) src = 0;
    else if (t == MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE) src = 1;
    else return MIXERR_INVALLINE;
    break;
  }
  case 4:  // TARGETTYPE
    if (w32_rd32(p + 120) != MIXERLINE_TARGETTYPE_WAVEIN) return MIXERR_INVALLINE;
    src = 0;
    break;
  default: return MMSYSERR_INVALFLAG;
  }
  fill_line(p, id, &ep, src);
  return MMSYSERR_NOERROR;
}

// controls: 1 destination volume, 2 destination mute, 3 source volume, 4 source mute
static void fill_control(uint32_t p, uint32_t cid) {
  uint8_t b[148] = {0};
  uint32_t v = 148, vol = (cid & 1) != 0;
  memcpy(b, &v, 4);
  memcpy(b + 4, &cid, 4);
  v = vol ? MIXERCONTROL_CONTROLTYPE_VOLUME : MIXERCONTROL_CONTROLTYPE_MUTE;
  memcpy(b + 8, &v, 4);
  v = 1; memcpy(b + 12, &v, 4);  // MIXERCONTROL_CONTROLF_UNIFORM
  snprintf((char *)b + 20, 16, "%s", vol ? "Volume" : "Mute");
  snprintf((char *)b + 36, 64, "%s", vol ? "Volume" : "Mute");
  v = 0; memcpy(b + 100, &v, 4);
  v = vol ? 65535 : 1; memcpy(b + 104, &v, 4);
  v = vol ? 65535 : 0; memcpy(b + 124, &v, 4);  // cSteps
  memcpy(RC32_H(p), b, 148);
}

// mixerGetLineControlsA(HMIXEROBJ, MIXERLINECONTROLSA *, DWORD flags)
static uint64_t mx_GetLineControlsA(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  uint32_t id, p = a[1], q = a[2] & 0xf;
  if (!p || w32_rd32(p) < 24) return MMSYSERR_INVALPARAM;
  if (!mixer_id(a[0], a[2], &id)) return MMSYSERR_INVALHANDLE;
  uint32_t line = w32_rd32(p + 4), arr = w32_rd32(p + 20), cb = w32_rd32(p + 16);
  if (!arr || cb < 148) return MMSYSERR_INVALPARAM;
  uint32_t base = line == LINE_SRC ? 3 : 1;
  if (q == 0) {  // ALL
    if (line != LINE_DST && line != LINE_SRC) return MIXERR_INVALLINE;
    fill_control(arr, base);
    fill_control(arr + cb, base + 1);
    w32_wr32(p + 12, 2);
  } else if (q == 1) {  // ONEBYID
    uint32_t cid = w32_rd32(p + 8);
    if (cid < 1 || cid > 4) return MIXERR_INVALCONTROL;
    w32_wr32(p + 4, cid >= 3 ? LINE_SRC : LINE_DST);
    fill_control(arr, cid);
  } else if (q == 2) {  // ONEBYTYPE
    if (line != LINE_DST && line != LINE_SRC) return MIXERR_INVALLINE;
    uint32_t type = w32_rd32(p + 8);
    if (type == MIXERCONTROL_CONTROLTYPE_VOLUME) fill_control(arr, base);
    else if (type == MIXERCONTROL_CONTROLTYPE_MUTE) fill_control(arr, base + 1);
    else return MIXERR_INVALCONTROL;
  } else {
    return MMSYSERR_INVALFLAG;
  }
  return MMSYSERR_NOERROR;
}

// MIXERCONTROLDETAILS: cbStruct, dwControlID, cChannels, cMultipleItems, cbDetails, paDetails
static uint64_t control_details(uint32_t hmx, uint32_t p, uint32_t flags, int set) {
  uint32_t id;
  wa_endpoint ep;
  if (!p || w32_rd32(p) < 24) return MMSYSERR_INVALPARAM;
  if (!mixer_id(hmx, flags, &id) || !in_device(id, &ep)) return MMSYSERR_INVALHANDLE;
  if ((flags & 0xf) != 0) return MMSYSERR_NOTSUPPORTED;  // only MIXER_*CONTROLDETAILSF_VALUE
  uint32_t cid = w32_rd32(p + 4), ch = w32_rd32(p + 8), cbd = w32_rd32(p + 16), d = w32_rd32(p + 20);
  if (cid < 1 || cid > 4) return MIXERR_INVALCONTROL;
  if (!d || cbd < 4) return MMSYSERR_INVALPARAM;
  if (!ch) ch = 1;
  int vol = cid & 1;
  if (set) {
    uint32_t v = w32_rd32(d);
    if (vol) wa_endpoint_set_volume(&ep, v / 65535.f);
    else wa_endpoint_set_mute(&ep, v != 0);
    rs_audio_log("mixer %u: %s %u", id, vol ? "volume" : "mute", v);
  } else {
    uint32_t v = vol ? (uint32_t)lrintf(wa_endpoint_volume(&ep) * 65535.f) : (uint32_t)wa_endpoint_mute(&ep);
    for (uint32_t i = 0; i < ch && i < 8; i++) w32_wr32(d + i * cbd, v);
  }
  return MMSYSERR_NOERROR;
}
static uint64_t mx_GetControlDetailsA(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; return control_details(a[0], a[1], a[2], 0); }
static uint64_t mx_SetControlDetails(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; return control_details(a[0], a[1], a[2], 1); }

// ------------------------------------------------------------------------------------------------
// Timers: the scheduler's resolution is already fine-grained on the Mac
// ------------------------------------------------------------------------------------------------
static uint64_t tm_Period(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)self; (void)a; return 0; }
static uint64_t tm_GetDevCaps(rc_ctx *c, uint32_t self, const uint32_t *a) {
  (void)c; (void)self;
  if (!a[0] || a[1] < 8) return MMSYSERR_INVALPARAM;  // TIMERR_NOCANDO is 97; this is the same for callers
  w32_wr32(a[0], 1);
  w32_wr32(a[0] + 4, 1000000);
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Exports
// ------------------------------------------------------------------------------------------------
typedef struct {
  const char *name;
  int words;
  w32_hostfn fn;
  _Atomic uint32_t cell;
} wa_export;

static wa_export k_exports[] = {
    {"waveInGetNumDevs", 0, wi_GetNumDevs, 0},
    {"waveInGetDevCapsA", 3, wi_GetDevCapsA, 0},
    {"waveInGetDevCapsW", 3, wi_GetDevCapsW, 0},
    {"waveInGetID", 2, wi_GetID, 0},
    {"waveInOpen", 6, wi_Open, 0},
    {"waveInClose", 1, wi_Close, 0},
    {"waveInPrepareHeader", 3, wi_PrepareHeader, 0},
    {"waveInUnprepareHeader", 3, wi_UnprepareHeader, 0},
    {"waveInAddBuffer", 3, wi_AddBuffer, 0},
    {"waveInStart", 1, wi_Start, 0},
    {"waveInStop", 1, wi_Stop, 0},
    {"waveInReset", 1, wi_Reset, 0},
    {"waveInGetPosition", 3, wi_GetPosition, 0},
    {"mixerGetNumDevs", 0, mx_GetNumDevs, 0},
    {"mixerOpen", 5, mx_Open, 0},
    {"mixerClose", 1, mx_Close, 0},
    {"mixerGetID", 3, mx_GetID, 0},
    {"mixerGetDevCapsA", 3, mx_GetDevCapsA, 0},
    {"mixerGetLineInfoA", 3, mx_GetLineInfoA, 0},
    {"mixerGetLineControlsA", 3, mx_GetLineControlsA, 0},
    {"mixerGetControlDetailsA", 3, mx_GetControlDetailsA, 0},
    {"mixerSetControlDetails", 3, mx_SetControlDetails, 0},
    {"timeBeginPeriod", 1, tm_Period, 0},
    {"timeEndPeriod", 1, tm_Period, 0},
    {"timeGetDevCaps", 2, tm_GetDevCaps, 0},
};
#define N_EXPORTS (int)(sizeof k_exports / sizeof k_exports[0])

static wa_export *export_of(const char *name) {
  const char *bang = strchr(name, '!');
  if (bang) {
    if ((size_t)(bang - name) != 9 || strncasecmp(name, "winmm.dll", 9) != 0) return NULL;
    name = bang + 1;
  }
  for (int i = 0; i < N_EXPORTS; i++)
    if (!strcmp(k_exports[i].name, name)) return &k_exports[i];
  return NULL;
}

uint32_t audio_import(const char *name) {
  wa_export *e = export_of(name);
  if (!e) return 0;
  uint32_t g = atomic_load(&e->cell);
  if (!g) {
    uint32_t n = w32_hostfn_new(e->name, W32_STDCALL, e->words, e->fn, 0);
    uint32_t zero = 0;
    if (!atomic_compare_exchange_strong(&e->cell, &zero, n)) g = zero;  // another thread made one first
    else g = n;
  }
  return g;
}

int audio_dispatch(rc_ctx *c, const char *name) {
  wa_export *e = export_of(name);
  if (!e) return 0;
  uint32_t a[8], esp = (uint32_t)c->r[4];
  for (int k = 0; k < e->words; k++) a[k] = w32_rd32(esp + 4 * (uint32_t)k);
  uint64_t r = e->fn(c, 0, a);
  c->r[0] = (uint32_t)r;
  c->r[2] = (uint32_t)(r >> 32);
  c->r[4] = esp + 4 * (uint32_t)e->words;
  return 1;
}
