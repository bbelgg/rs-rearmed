// The win-x86 audio faces through the guest ABI, without the game: COM methods called through
// their vtable cells with the arguments on a guest stack (stdcall, callee pops), winmm functions
// through their import cells, events made and waited on with the kernel32 shims, and a waveIn
// callback into a "guest" function (a C function in rc32_table at a fake address).
//
// It drives the calls the game's PortAudio makes: device enumeration and properties, a shared
// event-driven render stream at the mix format (about 0.6 s of a quiet 440 Hz tone), an
// exclusive event-mode stream as the game's Wwise sink opens it (0.3 s, 256-frame periods), the
// guitar input as the game opens it (exclusive, event mode, 48 kHz mono float) when there is a
// capture device, and waveIn / mixer as NSoundInputMgr_Win32 uses them.
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "rs_audio_input.h"
#include "wa_engine.h"
#include "win32_guestobj.h"
#include "win32_shims.h"

void shim_CreateEventW(rc_ctx *c);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define CHECK_HR(x) do { uint32_t hr_ = (uint32_t)(x); if (hr_ != 0) { fprintf(stderr, "FAIL %s:%d: %s = 0x%08x\n", __FILE__, __LINE__, #x, hr_); exit(1); } } while (0)

// --- fake guest code: the waveIn callback ---------------------------------------------------
// rc32_call finds guest functions through the loaded images, so the table is registered as an
// image: a PE with no sections at FAKE_BASE (fake_image below).
enum { FAKE_BASE = 0x00400000, G_WAVEIN_PROC = FAKE_BASE + 0x100, G_DEFAULT_CHANGED = FAKE_BASE + 0x200 };
static _Atomic int wim_open, wim_close, wim_data;
static _Atomic uint32_t wim_bytes;
static void f_wavein_proc(rc_ctx *c) {  // void CALLBACK (HWAVEIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2)
  uint32_t esp = (uint32_t)c->r[4];
  uint32_t msg = w32_rd32(esp + 8), p1 = w32_rd32(esp + 16);
  if (msg == 0x3BE) wim_open++;
  if (msg == 0x3BF) wim_close++;
  if (msg == 0x3C0) {
    wim_data++;
    wim_bytes += w32_rd32(p1 + 8);
  }
  c->ret_to = w32_rd32(esp);
  c->r[4] = esp + 4 + 20;  // stdcall: 5 words
}
// IMMNotificationClient::OnDefaultDeviceChanged(this, flow, role, id)
static _Atomic int default_changed_calls;
static void f_default_changed(rc_ctx *c) {
  uint32_t esp = (uint32_t)c->r[4];
  if (w32_rd32(esp + 8) == 0) default_changed_calls++;  // eRender
  c->ret_to = w32_rd32(esp);
  c->r[0] = 0;
  c->r[4] = esp + 4 + 16;  // stdcall: 4 words
}
const rc32_entry rc32_table[] = {{G_WAVEIN_PROC, f_wavein_proc}, {G_DEFAULT_CHANGED, f_default_changed}};
const int rc32_table_len = 2;
const rc32_import rc32_imports[] = {{0, 0, 0}};
const int rc32_imports_len = 0;
static rc32_module fake_mod = {"", rc32_table, &rc32_table_len, rc32_imports, &rc32_imports_len, 0, 0};

static void fake_image(void) {
  uint8_t pe[0x200] = {'M', 'Z'};
  uint32_t o = 0x40, base = FAKE_BASE, size = 0x1000, hdrs = 0x200, ndirs = 16;
  memcpy(pe + 0x3c, &o, 4);
  memcpy(pe + o, "PE\0\0", 4);
  uint16_t mach = 0x14c, nsect = 0, optsz = 0xe0, chars = 0x0102, magic = 0x10b;
  memcpy(pe + o + 4, &mach, 2);
  memcpy(pe + o + 6, &nsect, 2);
  memcpy(pe + o + 20, &optsz, 2);
  memcpy(pe + o + 22, &chars, 2);
  uint8_t *opt = pe + o + 24;
  memcpy(opt, &magic, 2);
  memcpy(opt + 28, &base, 4);
  memcpy(opt + 56, &size, 4);
  memcpy(opt + 60, &hdrs, 4);
  memcpy(opt + 92, &ndirs, 4);
  const char *tmp = getenv("TMPDIR");
  char path[512];
  snprintf(path, sizeof path, "%s/test_audio_fake_%d.exe", tmp && *tmp ? tmp : "/tmp", (int)getpid());
  FILE *f = fopen(path, "wb");
  if (!f || fwrite(pe, 1, sizeof pe, f) != sizeof pe) { fprintf(stderr, "FAIL: cannot write %s\n", path); exit(1); }
  fclose(f);
  if (!rc32_image_add(&fake_mod, path)) { fprintf(stderr, "FAIL: rc32_image_add\n"); exit(1); }
  remove(path);
}

// --- calling as guest code would --------------------------------------------------------------
static rc_ctx *C;

// stdcall method: this + n words; checks the callee removed them all
static uint32_t vcall(uint32_t obj, int slot, int n, const uint32_t *a) {
  uint32_t esp0 = (uint32_t)C->r[4];
  C->r[4] -= 4u * (uint32_t)n + 8;
  w32_wr32((uint32_t)C->r[4], 0xdead0000u);
  w32_wr32((uint32_t)C->r[4] + 4, obj);
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)C->r[4] + 8 + 4 * k, a[k]);
  CHECK(w32_hostfn_call(C, w32_rd32(w32_rd32(obj) + 4 * (uint32_t)slot)));
  CHECK(C->ret_to == 0xdead0000u);
  CHECK((uint32_t)C->r[4] == esp0);
  return (uint32_t)C->r[0];
}
// a winmm import through its cell
static uint32_t wcall(const char *name, int n, const uint32_t *a) {
  uint32_t cell = audio_import(name);
  CHECK(cell);
  uint32_t esp0 = (uint32_t)C->r[4];
  C->r[4] -= 4u * (uint32_t)n + 4;
  w32_wr32((uint32_t)C->r[4], 0xdead0000u);
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)C->r[4] + 4 + 4 * k, a[k]);
  CHECK(w32_hostfn_call(C, cell));
  CHECK((uint32_t)C->r[4] == esp0);
  return (uint32_t)C->r[0];
}
// a kernel32 shim (import convention: arguments at esp, no return address)
static uint32_t create_event(int manual) {
  uint32_t esp0 = (uint32_t)C->r[4];
  C->r[4] -= 16;
  uint32_t a[4] = {0, (uint32_t)manual, 0, 0};
  for (int k = 0; k < 4; k++) w32_wr32((uint32_t)C->r[4] + 4 * k, a[k]);
  shim_CreateEventW(C);
  CHECK((uint32_t)C->r[4] == esp0);
  return (uint32_t)C->r[0];
}

static uint32_t guid(const char *s) {  // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
  unsigned d1, d2, d3, b[8];
  CHECK(sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6],
               &b[7]) == 11);
  uint32_t g = w32_galloc(16);
  uint8_t *p = RC32_H(g);
  memcpy(p, &d1, 4);
  uint16_t w2 = (uint16_t)d2, w3 = (uint16_t)d3;
  memcpy(p + 4, &w2, 2);
  memcpy(p + 6, &w3, 2);
  for (int i = 0; i < 8; i++) p[8 + i] = (uint8_t)b[i];
  return g;
}
static uint32_t W(void) { return w32_galloc(16); }  // an out-parameter cell
static uint32_t rd(uint32_t g) { return w32_rd32(g); }
static uint64_t rd64(uint32_t g) { uint64_t v; memcpy(&v, RC32_H(g), 8); return v; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void narrow(uint32_t w, char *out, size_t n) {
  const uint16_t *s = RC32_H(w);
  size_t k = 0;
  while (s[k] && k + 1 < n) { out[k] = s[k] < 128 ? (char)s[k] : '?'; k++; }
  out[k] = 0;
}

enum { QI, ADDREF, RELEASE };
// IMMDeviceEnumerator
enum { E_ENUM = 3, E_DEFAULT, E_GETDEVICE, E_REGISTER, E_UNREGISTER };
// IMMDevice / collection / property store / endpoint
enum { D_ACTIVATE = 3, D_PROPS, D_GETID, D_STATE };
enum { L_COUNT = 3, L_ITEM };
enum { P_COUNT = 3, P_GETAT, P_GETVALUE };
// IAudioClient
enum { C_INIT = 3, C_BUFSIZE, C_LATENCY, C_PADDING, C_ISFMT, C_MIXFMT, C_PERIOD, C_START, C_STOP, C_RESET, C_SETEVENT, C_SERVICE };

static uint32_t IID_IUnknown, IID_IMMDeviceEnumerator, IID_IMMEndpoint, IID_IAudioClient, IID_IAudioRenderClient,
    IID_IAudioCaptureClient, IID_IAudioEndpointVolume, CLSID_MMDeviceEnumerator, PKEY_FriendlyName, PKEY_DeviceFormat,
    PKEY_FormFactor;

static uint32_t pkey(const char *fmtid, uint32_t pid) {
  uint32_t g = guid(fmtid), k = w32_galloc(20);
  memcpy(RC32_H(k), RC32_H(g), 16);
  w32_wr32(k + 16, pid);
  return k;
}

static void list_endpoints(uint32_t en) {
  uint32_t pl = W();
  CHECK_HR(vcall(en, E_ENUM, 3, (uint32_t[]){2, 1, pl}));  // eAll, DEVICE_STATE_ACTIVE
  uint32_t coll = rd(pl), pn = W();
  CHECK_HR(vcall(coll, L_COUNT, 1, (uint32_t[]){pn}));
  printf("endpoints: %u\n", rd(pn));
  for (uint32_t i = 0; i < rd(pn); i++) {
    uint32_t pd = W(), pid = W(), pst = W(), pps = W(), pv = w32_galloc(16), pe = W(), pf = W();
    CHECK_HR(vcall(coll, L_ITEM, 2, (uint32_t[]){i, pd}));
    uint32_t dev = rd(pd);
    CHECK_HR(vcall(dev, D_GETID, 1, (uint32_t[]){pid}));
    CHECK_HR(vcall(dev, D_STATE, 1, (uint32_t[]){pst}));
    CHECK(rd(pst) == 1);
    CHECK_HR(vcall(dev, D_PROPS, 2, (uint32_t[]){0, pps}));
    uint32_t props = rd(pps);
    char name[128], id[256], inst[128] = "";
    CHECK_HR(vcall(props, P_GETVALUE, 2, (uint32_t[]){PKEY_FriendlyName, pv}));
    CHECK((rd(pv) & 0xffff) == 31);
    narrow(rd(pv + 8), name, sizeof name);
    wa_propvariant_clear(pv);
    CHECK_HR(vcall(props, P_GETVALUE, 2, (uint32_t[]){PKEY_DeviceFormat, pv}));
    CHECK((rd(pv) & 0xffff) == 65 && rd(pv + 8) == 40);
    uint32_t wfx = rd(pv + 12);
    uint16_t ch = (uint16_t)rd(wfx + 2);
    uint32_t rate = rd(wfx + 4);
    wa_propvariant_clear(pv);
    // every key the store lists: the game reads the USB VID/PID from one of them
    uint32_t pc = W(), key = w32_galloc(20);
    CHECK_HR(vcall(props, P_COUNT, 1, (uint32_t[]){pc}));
    for (uint32_t k = 0; k < rd(pc); k++) {
      CHECK_HR(vcall(props, P_GETAT, 2, (uint32_t[]){k, key}));
      CHECK_HR(vcall(props, P_GETVALUE, 2, (uint32_t[]){key, pv}));
      if ((rd(pv) & 0xffff) == 31) {
        char s[128];
        narrow(rd(pv + 8), s, sizeof s);
        if (strstr(s, "VID_") || strstr(s, "ROOT")) snprintf(inst, sizeof inst, "%s", s);
      }
      wa_propvariant_clear(pv);
    }
    CHECK_HR(vcall(dev, QI, 2, (uint32_t[]){IID_IMMEndpoint, pe}));
    CHECK_HR(vcall(rd(pe), 3, 1, (uint32_t[]){pf}));
    narrow(rd(pid), id, sizeof id);
    printf("  [%u] %s \"%s\" %u Hz x %u  %s\n", i, rd(pf) ? "capture" : "render ", name, rate, ch, inst);
    CHECK(strstr(id, rd(pf) ? "{0.0.1." : "{0.0.0.") == id);
    wa_taskmem_free(rd(pid));
    vcall(rd(pe), RELEASE, 0, 0);
    vcall(props, RELEASE, 0, 0);
    // GetDevice finds it again by its id
    uint32_t pd2 = W(), pid2 = W();
    CHECK_HR(vcall(dev, D_GETID, 1, (uint32_t[]){pid2}));
    CHECK_HR(vcall(en, E_GETDEVICE, 2, (uint32_t[]){rd(pid2), pd2}));
    vcall(rd(pd2), RELEASE, 0, 0);
    CHECK(vcall(dev, RELEASE, 0, 0) == 0);
  }
  CHECK(vcall(coll, RELEASE, 0, 0) == 0);
}

typedef struct {
  double secs;
  uint64_t written;
  uint32_t wakeups, timeouts, last_padding, buffer, period;
} run_stats;

// Render through IAudioRenderClient in event mode: fill what the buffer has free at each event.
static void render_run(uint32_t client, uint32_t wfx, int exclusive, double secs, run_stats *st) {
  uint16_t ch = (uint16_t)rd(wfx + 2);
  uint32_t rate = rd(wfx + 4);
  int is_float = (rd(wfx) & 0xffff) == 0xfffe ? rd(wfx + 24) == 3 : (rd(wfx) & 0xffff) == 3;
  CHECK(is_float && (rd(wfx + 14) & 0xffff) == 32);
  uint32_t ppl = W(), pbuf = W(), pdata = W(), ppad = W(), ev = create_event(0);
  CHECK_HR(vcall(client, C_SERVICE, 2, (uint32_t[]){IID_IAudioRenderClient, ppl}));
  uint32_t rc = rd(ppl);
  CHECK_HR(vcall(client, C_SETEVENT, 1, (uint32_t[]){ev}));
  CHECK_HR(vcall(client, C_BUFSIZE, 1, (uint32_t[]){pbuf}));
  uint32_t buffer = rd(pbuf);
  memset(st, 0, sizeof *st);
  st->buffer = buffer;
  double phase = 0, inc = 2 * M_PI * 440.0 / rate;
  // prefill, as PortAudio does before Start
  #define FILL(n)                                                                   \
    do {                                                                            \
      CHECK_HR(vcall(rc, 3, 2, (uint32_t[]){(n), pdata}));                          \
      float *f = RC32_H(rd(pdata));                                                 \
      for (uint32_t i = 0; i < (n); i++, phase += inc)                              \
        for (uint16_t k = 0; k < ch; k++) f[i * ch + k] = (float)(0.05 * sin(phase)); \
      CHECK_HR(vcall(rc, 4, 2, (uint32_t[]){(n), 0}));                              \
      st->written += (n);                                                           \
    } while (0)
  FILL(buffer);
  CHECK_HR(vcall(client, C_START, 0, 0));
  CHECK(vcall(client, C_START, 0, 0) == 0x88890005u);  // AUDCLNT_E_NOT_STOPPED
  double t0 = now();
  while (now() - t0 < secs) {
    uint32_t h[1] = {ev};
    uint32_t r = w32_wait_multiple(1, h, 0, 200);
    if (r != 0) { st->timeouts++; continue; }
    st->wakeups++;
    uint32_t n;
    if (exclusive) {
      n = buffer;  // a whole period each event, as PortAudio's exclusive event thread writes
    } else {
      CHECK_HR(vcall(client, C_PADDING, 1, (uint32_t[]){ppad}));
      n = buffer - rd(ppad);
    }
    if (n) FILL(n);
  }
  st->secs = now() - t0;
  CHECK_HR(vcall(client, C_PADDING, 1, (uint32_t[]){ppad}));
  st->last_padding = rd(ppad);
  CHECK_HR(vcall(client, C_STOP, 0, 0));
  vcall(rc, RELEASE, 0, 0);
  #undef FILL
}

static void test_render(uint32_t en) {
  uint32_t pd = W();
  uint32_t hr = vcall(en, E_DEFAULT, 3, (uint32_t[]){0, 1, pd});  // eRender, eMultimedia
  if (hr == 0x80070490u) {
    printf("render: no output device, skipped\n");
    return;
  }
  CHECK_HR(hr);
  uint32_t dev = rd(pd), pc = W(), pmix = W(), pdef = W(), pmin = W(), plat = W();
  // shared, at the mix format, event-driven
  CHECK_HR(vcall(dev, D_ACTIVATE, 4, (uint32_t[]){IID_IAudioClient, 1, 0, pc}));
  uint32_t client = rd(pc);
  CHECK(vcall(client, C_BUFSIZE, 1, (uint32_t[]){W()}) == 0x88890001u);  // AUDCLNT_E_NOT_INITIALIZED
  CHECK_HR(vcall(client, C_MIXFMT, 1, (uint32_t[]){pmix}));
  uint32_t mix = rd(pmix), pclosest = W();
  CHECK_HR(vcall(client, C_ISFMT, 3, (uint32_t[]){0, mix, pclosest}));
  CHECK(rd(pclosest) == 0);
  CHECK_HR(vcall(client, C_PERIOD, 2, (uint32_t[]){pdef, pmin}));
  uint64_t period = rd64(pdef);
  printf("render: mix %u Hz x %u, device period %.2f ms (min %.2f ms)\n", rd(mix + 4), rd(mix + 2) & 0xffff,
         period / 1e4, rd64(pmin) / 1e4);
  // a 44.1 kHz request in shared mode gets the mix format back as the closest match
  uint32_t w44 = w32_galloc(40);
  memcpy(RC32_H(w44), RC32_H(mix), 40);
  w32_wr32(w44 + 4, 44100);
  w32_wr32(w44 + 8, 44100 * (rd(mix + 12) & 0xffff));
  CHECK(vcall(client, C_ISFMT, 3, (uint32_t[]){0, w44, pclosest}) == 1);
  CHECK(rd(pclosest) && rd(rd(pclosest) + 4) == 48000);
  wa_taskmem_free(rd(pclosest));

  uint64_t dur = 2 * period;
  CHECK_HR(vcall(client, C_INIT, 8, (uint32_t[]){0, 0x40000, (uint32_t)dur, (uint32_t)(dur >> 32), 0, 0, mix, 0}));
  CHECK(vcall(client, C_INIT, 8, (uint32_t[]){0, 0x40000, (uint32_t)dur, 0, 0, 0, mix, 0}) == 0x88890002u);
  CHECK(vcall(client, C_START, 0, 0) == 0x88890015u);  // event mode without an event: AUDCLNT_E_EVENTHANDLE_NOT_SET
  CHECK_HR(vcall(client, C_LATENCY, 1, (uint32_t[]){plat}));
  run_stats s;
  render_run(client, mix, 0, 0.6, &s);
  uint32_t rate = rd(mix + 4);
  uint64_t played = s.written - s.last_padding;
  double expect = s.secs * rate;
  printf("render shared: buffer %u frames, stream latency %.2f ms; %.3f s: %llu frames written, %llu played "
         "(%.1f%% of real time), %u events, %u timeouts\n",
         s.buffer, rd64(plat) / 1e4, s.secs, (unsigned long long)s.written, (unsigned long long)played,
         100.0 * played / expect, s.wakeups, s.timeouts);
  CHECK(s.timeouts == 0);
  CHECK(played > 0.85 * expect && played < 1.15 * expect + s.buffer);
  CHECK(s.wakeups > s.secs * rate / s.buffer);  // at least one event per buffer's worth
  vcall(client, RELEASE, 0, 0);

  // exclusive, event-driven, a 256-frame period: how the game's Wwise sink opens it
  CHECK_HR(vcall(dev, D_ACTIVATE, 4, (uint32_t[]){IID_IAudioClient, 1, 0, pc}));
  client = rd(pc);
  CHECK_HR(vcall(client, C_ISFMT, 3, (uint32_t[]){1, mix, 0}));
  uint64_t p256 = 256ull * 10000000ull / 48000;
  CHECK_HR(vcall(client, C_INIT, 8, (uint32_t[]){1, 0x40000, (uint32_t)p256, 0, (uint32_t)p256, 0, mix, 0}));
  render_run(client, mix, 1, 0.3, &s);
  played = s.written - s.last_padding;
  expect = s.secs * rate;
  printf("render exclusive: buffer %u frames; %.3f s: %u events (%.1f per %u frames of real time), %u timeouts\n",
         s.buffer, s.secs, s.wakeups, s.wakeups * (double)s.buffer / expect, s.buffer, s.timeouts);
  CHECK(s.timeouts == 0);
  CHECK(s.wakeups * (double)s.buffer > 0.85 * expect);  // the events keep pace with the device
  vcall(client, RELEASE, 0, 0);

  // a 44.1 kHz stream: the device period is the same frames, at the stream's rate
  CHECK_HR(vcall(dev, D_ACTIVATE, 4, (uint32_t[]){IID_IAudioClient, 1, 0, pc}));
  client = rd(pc);
  uint64_t p44 = 256ull * 10000000ull / 44100;
  CHECK_HR(vcall(client, C_INIT, 8, (uint32_t[]){1, 0x40000, (uint32_t)p44, 0, (uint32_t)p44, 0, w44, 0}));
  CHECK_HR(vcall(client, C_PERIOD, 2, (uint32_t[]){pdef, pmin}));
  uint64_t frames48 = (period * 48000 + 5000000) / 10000000, frames44 = (rd64(pdef) * 44100 + 5000000) / 10000000;
  printf("render at 44.1 kHz: device period %.2f ms, %llu frames (%llu at 48 kHz)\n", rd64(pdef) / 1e4,
         (unsigned long long)frames44, (unsigned long long)frames48);
  CHECK(frames44 == frames48);
  vcall(client, RELEASE, 0, 0);
  wa_taskmem_free(mix);
  CHECK(vcall(dev, RELEASE, 0, 0) == 0);
}

static void test_capture(uint32_t en) {
  uint32_t pd = W();
  uint32_t hr = vcall(en, E_DEFAULT, 3, (uint32_t[]){1, 1, pd});  // eCapture
  if (hr == 0x80070490u) {
    printf("capture: no input device, skipped\n");
    return;
  }
  CHECK_HR(hr);
  uint32_t dev = rd(pd), pc = W(), pv = W(), pvol = W(), pf = W();
  // the input's endpoint volume, as PortAudio activates it next to the client
  CHECK_HR(vcall(dev, D_ACTIVATE, 4, (uint32_t[]){IID_IAudioEndpointVolume, 1, 0, pv}));
  uint32_t vol = rd(pv);
  CHECK_HR(vcall(vol, 9, 1, (uint32_t[]){pvol}));  // GetMasterVolumeLevelScalar
  float f;
  memcpy(&f, RC32_H(pvol), 4);
  wa_endpoint in;
  CHECK(wa_default_endpoint(WA_CAPTURE, &in));
  CHECK(f == (rs_audio_level_on() && !rs_audio_is_cable(in.dev) ? RS_AUDIO_LEVEL_UNITY : 1.f));  // an input's level
  float half = 0.5f, one = 1.f;
  uint32_t hw, ow;
  memcpy(&hw, &half, 4);
  memcpy(&ow, &one, 4);
  CHECK_HR(vcall(vol, 7, 2, (uint32_t[]){hw, 0}));  // SetMasterVolumeLevelScalar
  CHECK_HR(vcall(vol, 9, 1, (uint32_t[]){pvol}));
  memcpy(&f, RC32_H(pvol), 4);
  CHECK(f == 0.5f);
  CHECK_HR(vcall(vol, 7, 2, (uint32_t[]){ow, 0}));
  // the guitar input as the game opens it: 48 kHz mono float, exclusive, event mode, 256 frames
  CHECK_HR(vcall(dev, D_ACTIVATE, 4, (uint32_t[]){IID_IAudioClient, 1, 0, pc}));
  uint32_t client = rd(pc), wfx = w32_galloc(40);
  uint8_t w[40] = {0xfe, 0xff, 1, 0, 0x80, 0xbb, 0, 0, 0, 0xee, 2, 0, 4, 0, 32, 0, 22, 0, 32, 0, 4, 0, 0, 0,
                   3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
  memcpy(RC32_H(wfx), w, 40);
  CHECK_HR(vcall(client, C_ISFMT, 3, (uint32_t[]){1, wfx, 0}));
  uint64_t p256 = 256ull * 10000000ull / 48000;
  CHECK_HR(vcall(client, C_INIT, 8, (uint32_t[]){1, 0x40000, (uint32_t)p256, 0, (uint32_t)p256, 0, wfx, 0}));
  uint32_t pcc = W(), ev = create_event(0), pbuf = W();
  CHECK_HR(vcall(client, C_BUFSIZE, 1, (uint32_t[]){pbuf}));
  uint32_t buffer = rd(pbuf);
  CHECK(vcall(client, C_SERVICE, 2, (uint32_t[]){IID_IAudioRenderClient, pcc}) == 0x88890003u);  // wrong endpoint type
  CHECK_HR(vcall(client, C_SERVICE, 2, (uint32_t[]){IID_IAudioCaptureClient, pcc}));
  uint32_t cc = rd(pcc);
  CHECK_HR(vcall(client, C_SETEVENT, 1, (uint32_t[]){ev}));
  CHECK_HR(vcall(client, C_START, 0, 0));
  uint32_t pdata = W(), pn = W(), pflags = W(), ppos = w32_galloc(8), pnext = W();
  uint32_t packets = 0, timeouts = 0;
  uint64_t frames = 0;
  double peak = 0, t0 = now();
  while (now() - t0 < 0.3) {
    uint32_t h[1] = {ev};
    if (w32_wait_multiple(1, h, 0, 200) != 0) { timeouts++; continue; }
    for (;;) {
      CHECK_HR(vcall(cc, 5, 1, (uint32_t[]){pnext}));  // GetNextPacketSize
      if (!rd(pnext)) break;
      uint32_t r = vcall(cc, 3, 5, (uint32_t[]){pdata, pn, pflags, ppos, 0});
      if (r == 0x08890001u) break;  // AUDCLNT_S_BUFFER_EMPTY
      CHECK_HR(r);
      const float *x = RC32_H(rd(pdata));
      for (uint32_t i = 0; i < rd(pn); i++) peak = fabs(x[i]) > peak ? fabs(x[i]) : peak;
      frames += rd(pn);
      packets++;
      CHECK_HR(vcall(cc, 4, 1, (uint32_t[]){rd(pn)}));
    }
  }
  double secs = now() - t0;
  CHECK_HR(vcall(client, C_STOP, 0, 0));
  printf("capture exclusive: buffer %u frames; %.3f s: %u packets, %llu frames (%.1f%% of real time), peak %.3f, "
         "%u timeouts\n",
         buffer, secs, packets, (unsigned long long)frames, 100.0 * frames / (secs * 48000), peak, timeouts);
  CHECK(timeouts == 0);
  CHECK(frames > 0.7 * secs * 48000);  // the first packet waits for the device to start
  vcall(cc, RELEASE, 0, 0);
  vcall(client, RELEASE, 0, 0);
  vcall(vol, RELEASE, 0, 0);
  (void)pf;
  CHECK(vcall(dev, RELEASE, 0, 0) == 0);
}

static void test_wavein(void) {
  uint32_t n = wcall("winmm.dll!waveInGetNumDevs", 0, 0);
  printf("waveIn: %u devices\n", n);
  if (!n) {
    printf("waveIn: no input device, skipped\n");
    return;
  }
  uint32_t caps = w32_galloc(48);
  for (uint32_t i = 0; i < n; i++) {
    CHECK(wcall("waveInGetDevCapsA", 3, (uint32_t[]){i, caps, 48}) == 0);
    printf("  [%u] \"%s\" %u ch%s\n", i, (const char *)RC32_H(caps + 8), rd(caps + 44) & 0xffff,
           strstr((const char *)RC32_H(caps + 8), "Rocksmith") ? "  <- the game picks this one" : "");
  }
  CHECK(wcall("waveInGetDevCapsA", 3, (uint32_t[]){n, caps, 48}) == 2);  // MMSYSERR_BADDEVICEID
  // as NSoundInputMgr_Win32 opens it: 48 kHz mono float, CALLBACK_FUNCTION, 128-frame buffers
  uint32_t wfx = w32_galloc(18), ph = W();
  uint8_t w[18] = {3, 0, 1, 0, 0x80, 0xbb, 0, 0, 0, 0xee, 2, 0, 4, 0, 32, 0, 0, 0};
  memcpy(RC32_H(wfx), w, 18);
  CHECK(wcall("waveInOpen", 6, (uint32_t[]){ph, 0, wfx, G_WAVEIN_PROC, 0x1234, 0x30000}) == 0);
  CHECK(wim_open == 1);
  uint32_t h = rd(ph), pid = W();
  CHECK(wcall("waveInGetID", 2, (uint32_t[]){h, pid}) == 0 && rd(pid) == 0);
  enum { NB = 8, BYTES = 128 * 4 };
  uint32_t hdrs = w32_galloc(32 * NB), data = w32_galloc(BYTES * NB);
  for (int i = 0; i < NB; i++) {
    uint32_t hd = hdrs + 32u * (uint32_t)i;
    w32_wr32(hd, data + BYTES * (uint32_t)i);
    w32_wr32(hd + 4, BYTES);
    CHECK(wcall("waveInAddBuffer", 3, (uint32_t[]){h, hd, 32}) == 34);  // WAVERR_UNPREPARED
    CHECK(wcall("waveInPrepareHeader", 3, (uint32_t[]){h, hd, 32}) == 0);
    CHECK(wcall("waveInAddBuffer", 3, (uint32_t[]){h, hd, 32}) == 0);
  }
  CHECK(wcall("waveInStart", 1, (uint32_t[]){h}) == 0);
  double t0 = now();
  while (wim_data < NB && now() - t0 < 0.5) {
    struct timespec ts = {0, 2000000};
    nanosleep(&ts, NULL);
  }
  double secs = now() - t0;
  CHECK(wcall("waveInStop", 1, (uint32_t[]){h}) == 0);
  printf("waveIn: %d of %d buffers back in %.3f s (%u bytes)\n", (int)wim_data, NB, secs, (unsigned)wim_bytes);
  CHECK(wim_data == NB);
  CHECK(wcall("waveInClose", 1, (uint32_t[]){h}) == 0);
  CHECK(wim_close == 1);
  for (int i = 0; i < NB; i++) CHECK(rd(hdrs + 32u * (uint32_t)i + 16) & 1);  // WHDR_DONE

  // the mixer's volume control on device 0, as the game reads and sets the input level
  uint32_t pmx = W();
  CHECK(wcall("mixerOpen", 5, (uint32_t[]){pmx, 0, 0, 0, 0}) == 0);
  uint32_t mx = rd(pmx), line = w32_galloc(168);
  w32_wr32(line, 168);
  w32_wr32(line + 24, 7);  // MIXERLINE_COMPONENTTYPE_DST_WAVEIN
  CHECK(wcall("mixerGetLineInfoA", 3, (uint32_t[]){mx, line, 3}) == 0);
  uint32_t lc = w32_galloc(24), ctl = w32_galloc(148);
  w32_wr32(lc, 24);
  w32_wr32(lc + 4, rd(line + 12));
  w32_wr32(lc + 8, 0x50030001);  // MIXERCONTROL_CONTROLTYPE_VOLUME
  w32_wr32(lc + 12, 1);
  w32_wr32(lc + 16, 148);
  w32_wr32(lc + 20, ctl);
  CHECK(wcall("mixerGetLineControlsA", 3, (uint32_t[]){mx, lc, 2}) == 0);
  uint32_t det = w32_galloc(24), val = W();
  w32_wr32(det, 24);
  w32_wr32(det + 4, rd(ctl + 4));
  w32_wr32(det + 8, 1);
  w32_wr32(det + 16, 4);
  w32_wr32(det + 20, val);
  CHECK(wcall("mixerGetControlDetailsA", 3, (uint32_t[]){mx, det, 0}) == 0);
  CHECK(rd(val) == 65535 || rd(val) == (uint32_t)lrintf(RS_AUDIO_LEVEL_UNITY * 65535.f));  // the cable's, an input's level
  w32_wr32(val, 32768);
  CHECK(wcall("mixerSetControlDetails", 3, (uint32_t[]){mx, det, 0}) == 0);
  w32_wr32(val, 0);
  CHECK(wcall("mixerGetControlDetailsA", 3, (uint32_t[]){mx, det, 0}) == 0 && rd(val) == 32768);
  w32_wr32(val, 65535);
  CHECK(wcall("mixerSetControlDetails", 3, (uint32_t[]){mx, det, 0}) == 0);
  CHECK(wcall("mixerClose", 1, (uint32_t[]){mx}) == 0);
  printf("mixer: line \"%s\", control \"%s\" %u..%u\n", (const char *)RC32_H(line + 56), (const char *)RC32_H(ctl + 36),
         rd(ctl + 100), rd(ctl + 104));
}

// The names the game sees: the cable keeps (or gets) its Windows name; any other "Rocksmith"
// device would make the game warn that the cable is not configured.
static void test_game_names(void) {
  char n[128];
  snprintf(n, sizeof n, "Rocksmith USB Guitar Adapter");
  wa_game_name(n, sizeof n, 0x12ba, 0x00ff);
  CHECK(!strcmp(n, "Rocksmith USB Guitar Adapter"));
  snprintf(n, sizeof n, "Rocksmith Guitar Adapter Mono");
  wa_game_name(n, sizeof n, 0x12ba, 0x00ff);
  CHECK(!strcmp(n, "Rocksmith USB Guitar Adapter"));
  snprintf(n, sizeof n, "Rocksmith Game (OBS) ");
  wa_game_name(n, sizeof n, 0, 0);
  CHECK(!strcmp(n, "RS Game (OBS) "));
  snprintf(n, sizeof n, "Rocksmith to Rocksmith");
  wa_game_name(n, sizeof n, 0x0ecb, 0x2111);
  CHECK(!strcmp(n, "RS to RS"));
  snprintf(n, sizeof n, "MacBook Air Microphone");
  wa_game_name(n, sizeof n, 0, 0);
  CHECK(!strcmp(n, "MacBook Air Microphone"));
}

// a default output change reaches a registered client for each of the three roles, and no other
void wa_default_changed(void);
static void notify_test(uint32_t en) {
  wa_endpoint ep;
  if (!wa_default_endpoint(WA_RENDER, &ep)) { printf("no default output: notification not tested\n"); return; }
  uint32_t vt = w32_galloc(32), client = w32_galloc(8);
  w32_wr32(vt + 24, G_DEFAULT_CHANGED);
  w32_wr32(client, vt);
  uint32_t a[1] = {client};
  CHECK_HR(vcall(en, 6, 1, a));
  wa_default_changed();
  for (int k = 0; k < 200 && default_changed_calls < 3; k++) usleep(10000);
  CHECK(default_changed_calls == 3);
  CHECK_HR(vcall(en, 7, 1, a));
  wa_default_changed();
  usleep(100000);
  CHECK(default_changed_calls == 3);
  printf("default output change: 3 calls, none after Unregister\n");
}

int main(int argc, char **argv) {
  CHECK(rc32_mem_init() == 0);
  fake_image();
  C = rc32_thread_ctx();
  CLSID_MMDeviceEnumerator = guid("bcde0395-e52f-467c-8e3d-c4579291692e");
  IID_IUnknown = guid("00000000-0000-0000-c000-000000000046");
  IID_IMMDeviceEnumerator = guid("a95664d2-9614-4f35-a746-de8db63617e6");
  IID_IMMEndpoint = guid("1be09788-6894-4089-8586-9a2a6c265ac5");
  IID_IAudioClient = guid("1cb9ad4c-dbfa-4c32-b178-c2f568a703b2");
  IID_IAudioRenderClient = guid("f294acfc-3146-4483-a7bf-addca7c260e2");
  IID_IAudioCaptureClient = guid("c8adbd64-e71e-48a0-a4de-185c395cd317");
  IID_IAudioEndpointVolume = guid("5cdf2c82-841e-4546-9722-0cf74078229a");
  PKEY_FriendlyName = pkey("a45c254e-df1c-4efd-8020-67d146a850e0", 14);
  PKEY_DeviceFormat = pkey("f19f064d-082c-4e27-bc73-6882a1bb8e4c", 0);
  PKEY_FormFactor = pkey("1da5d803-d492-4edd-8c23-e0c0ffee7f0e", 0);

  // events from host code
  uint32_t ev = create_event(0), h[1] = {ev};
  CHECK(w32_event_signal(ev) && w32_wait_multiple(1, h, 0, 0) == 0 && w32_wait_multiple(1, h, 0, 0) == 0x102);
  CHECK(!w32_event_signal(0x10000003u));  // a file handle is not an event

  // CoCreateInstance
  uint32_t pen = W();
  CHECK(wasapi_has_class(CLSID_MMDeviceEnumerator) && !wasapi_has_class(IID_IUnknown));
  CHECK(wasapi_cocreate(IID_IUnknown, IID_IMMDeviceEnumerator, pen) == 0x80040154u);  // REGDB_E_CLASSNOTREG
  CHECK_HR(wasapi_cocreate(CLSID_MMDeviceEnumerator, IID_IMMDeviceEnumerator, pen));
  notify_test(w32_rd32(pen));
  uint32_t en = rd(pen), pu = W();
  CHECK_HR(vcall(en, QI, 2, (uint32_t[]){IID_IUnknown, pu}));
  CHECK(rd(pu) == en && vcall(en, RELEASE, 0, 0) == 1);
  CHECK(vcall(en, QI, 2, (uint32_t[]){IID_IAudioClient, pu}) == 0x80004002u);
  CHECK_HR(vcall(en, E_REGISTER, 1, (uint32_t[]){0x5000}));
  CHECK_HR(vcall(en, E_UNREGISTER, 1, (uint32_t[]){0x5000}));
  (void)PKEY_FormFactor;

  test_game_names();
  list_endpoints(en);
  if (argc > 1 && !strcmp(argv[1], "--list")) return 0;  // the device list only, nothing opened
  test_render(en);
  test_capture(en);
  test_wavein();
  CHECK(vcall(en, RELEASE, 0, 0) == 0);
  printf("ok\n");
  return 0;
}
