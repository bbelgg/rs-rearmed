// Runs the translated bink2w32.dll without the game, on the game's own Win32 seam
// (win-x86/shims: every kernel32 / user32 / winmm / ole32 import Bink makes goes through
// w32_import, as it will in rs2014_lp), through the runtime's image registry: maps the DLL
// (rebased to 0x12000000: its own base is d3dx9_42's), binds it, runs its DllMain, then plays a
// .bk2 the way the game's video player does (Rocksmith2014.exe, the player at link address
// 0x00c410b0 and its frame step at 0x00c40f50; docs/windows/DESIGN.md "Video (Bink)"):
//
//   BinkSetSoundTrack(1, {0}); BinkSetIOSize(0x40000);
//   BinkOpen(name, BINKSNDTRACK | BINKNOSKIP | BINKIOSIZE [| BINKIOPROCESSOR: the game's own
//            file reader, left out here: Bink reads the file itself]);
//   BinkGetTrackID(bink, 0) -> BinkOpenTrack(bink, id);
//   per frame: [BinkWait]; BinkDoFrame; BinkCopyToBuffer(bink, dst, pitch, h, 0, 0,
//              BINKCOPYALL | BINKSURFACE32); BinkGetTrackData(track, pcm); BinkNextFrame
//   BinkCloseTrack; BinkClose.
//
// It checks the header (size, frame count, the audio track), that the frames are pictures
// (not constant, changing from frame to frame) and that the track gives PCM that is not
// silence, writes one frame as a PNG, and times the decode. LP_LOG=1 lists each Win32 import's
// first call (the seam Bink needs, in order).
//
// usage: bink_probe <bink2w32.dll rebased to 0x12000000> <file.bk2> [-n frames] [-png out.png]
//                   [-at frame] [-wait] [-v]
// Built against the DLL's translation (gen_image_pe.py --module bink2w32) and the Win32 seam:
// build.sh beside this file. The DLL, the video and everything made from them are build inputs,
// never committed.
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zlib.h>

#include "rc32.h"
#include "win32_shims.h"

// rc_tramp / rc_setjmp look functions up by address; for this process that is the 32-bit table
rc_fn rc_lookup(uint64_t addr) { return rc32_lookup((uint32_t)addr); }
// no main image in this process: the seam's rc32_import_call has nothing to index
const rc32_import rc32_imports[] = {{0}};
const int rc32_imports_len = 0;

extern rc32_module bink2w32_rc32_mod;

// bink.h flags
#define BINKSNDTRACK 0x00004000u
#define BINKNOSKIP 0x00080000u
#define BINKIOSIZE 0x01000000u
#define BINKIOPROCESSOR 0x02000000u
#define BINKSURFACE32 3u            // B, G, R, x bytes (D3DFMT_X8R8G8B8 in memory)
#define BINKCOPYALL 0x80000000u
#define GAME_OPEN_FLAGS (BINKIOPROCESSOR | BINKIOSIZE | BINKNOSKIP | BINKSNDTRACK)  // 0x03084000

// --- what the seam lacks for Bink (docs/windows/DESIGN.md: to move into win32_kernel32.c) -----------
// Bink's imports come here first; the rest go on to the seam's own rc32_host_import.
static void k_GetWindowsDirectoryA(rc_ctx *c) {  // (buf, size) -> length; "C:\Windows"
  static const char d[] = "C:\\Windows";
  uint32_t b = w32_arg(c, 0), cap = w32_arg(c, 1);
  if (cap <= sizeof d - 1) { w32_ret(c, sizeof d, 2); return; }
  memcpy(RC32_H(b), d, sizeof d);
  w32_ret(c, sizeof d - 1, 2);
}
static void k_GetSystemDirectoryA(rc_ctx *c) {  // the 32-bit system folder as a 32-bit process sees it
  static const char d[] = "C:\\Windows\\system32";
  uint32_t b = w32_arg(c, 0), cap = w32_arg(c, 1);
  if (cap <= sizeof d - 1) { w32_ret(c, sizeof d, 2); return; }
  memcpy(RC32_H(b), d, sizeof d);
  w32_ret(c, sizeof d - 1, 2);
}
static const w32_export extra[] = {
    {"kernel32.dll!GetWindowsDirectoryA", k_GetWindowsDirectoryA},
    {"kernel32.dll!GetSystemDirectoryA", k_GetSystemDirectoryA},
};
static void (*seam_host_import)(rc_ctx *c, const rc32_module *m, int k);
static void host_import(rc_ctx *c, const rc32_module *m, int k) {
  const char *n = m->imports[k].name;
  if (!w32_import(n))
    for (size_t j = 0; j < sizeof extra / sizeof extra[0]; j++)
      if (!strcasecmp(extra[j].name, n)) {
        static uint8_t seen[sizeof extra / sizeof extra[0]];
        if (!seen[j]++) fprintf(stderr, "bink_probe: not in the seam, the probe's own: %s\n", n);
        extra[j].fn(c);
        return;
      }
  seam_host_import(c, m, k);
}

static rc_ctx *C;
static uint32_t bink_base;
static int verbose, fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); fails++; } else if (verbose) fprintf(stderr, "ok   %s\n", #x); } while (0)

static uint32_t bink(const char *name, int n, ...) {  // the exports are stdcall: _BinkOpen@8
  uint32_t a[8];
  va_list ap;
  va_start(ap, n);
  for (int k = 0; k < n; k++) a[k] = va_arg(ap, uint32_t);
  va_end(ap);
  char dec[64];
  snprintf(dec, sizeof dec, "_%s@%d", name, 4 * n);
  uint32_t f = rc32_image_export(bink_base, dec);
  if (!f) { fprintf(stderr, "bink_probe: no export %s\n", dec); exit(2); }
  return w32_guest_call(C, f, W32_STDCALL, 0, a, n, 0);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// --- PNG (8-bit RGBA, one IDAT) ---------------------------------------------------------------
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
  uint8_t h[8];
  be32(h, n);
  memcpy(h + 4, type, 4);
  fwrite(h, 1, 8, f);
  if (n) fwrite(d, 1, n, f);
  uint32_t crc = (uint32_t)crc32(crc32(0, h + 4, 4), d, n);
  be32(h, crc);
  fwrite(h, 1, 4, f);
}
// bgrx: the frame as BinkCopyToBuffer left it (BINKSURFACE32)
static int write_png(const char *path, const uint8_t *bgrx, uint32_t w, uint32_t h, uint32_t pitch) {
  size_t raw_n = (size_t)h * (1 + 4 * w);
  uint8_t *raw = malloc(raw_n), *z = malloc(compressBound(raw_n));
  for (uint32_t y = 0; y < h; y++) {
    uint8_t *o = raw + (size_t)y * (1 + 4 * w);
    const uint8_t *s = bgrx + (size_t)y * pitch;
    *o++ = 0;
    for (uint32_t x = 0; x < w; x++, s += 4) { *o++ = s[2]; *o++ = s[1]; *o++ = s[0]; *o++ = 255; }
  }
  uLongf zn = compressBound(raw_n);
  compress2(z, &zn, raw, raw_n, 6);
  FILE *f = fopen(path, "wb");
  if (!f) { free(raw); free(z); return -1; }
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  uint8_t ihdr[13];
  be32(ihdr, w);
  be32(ihdr + 4, h);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;  // 8-bit RGBA
  fwrite(sig, 1, 8, f);
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (uint32_t)zn);
  chunk(f, "IEND", 0, 0);
  fclose(f);
  free(raw);
  free(z);
  return 0;
}

// --- the run ----------------------------------------------------------------------------------
typedef struct { const char *dll, *video, *png; int frames, at, wait; int rc; } probe_arg;

static int run(probe_arg *p) {
  C = rc32_thread_ctx();
  if (rc32_images_init(C)) { fprintf(stderr, "bink_probe: DllMain failed\n"); return 1; }
  fprintf(stderr, "DllMain(PROCESS_ATTACH) -> ok\n");

  // as the game: track 0 is the one to load, 256 KB of read buffer, then open
  uint32_t tracks = w32_galloc(4);
  w32_wr32(tracks, 0);
  bink("BinkSetSoundTrack", 2, 1, tracks);
  bink("BinkSetIOSize", 1, 0x40000);
  uint32_t flags = GAME_OPEN_FLAGS & ~BINKIOPROCESSOR;
  double t0 = now();
  uint32_t hb = bink("BinkOpen", 2, w32_gstr(p->video), flags);
  double t_open = now() - t0;
  if (!hb) {
    uint32_t e = bink("BinkGetError", 0);
    fprintf(stderr, "bink_probe: BinkOpen failed: %s\n", e ? (const char *)RC32_H(e) : "?");
    return 1;
  }
  // HBINK: Width, Height, Frames, FrameNum, LastFrameNum, FrameRate, FrameRateDiv (the game reads
  // Frames at +8)
  uint32_t w = w32_rd32(hb), h = w32_rd32(hb + 4), nframes = w32_rd32(hb + 8);
  uint32_t fr = w32_rd32(hb + 20), frd = w32_rd32(hb + 24);
  fprintf(stderr, "BinkOpen(flags 0x%x) -> 0x%x in %.1f ms: %ux%u, %u frames at %u/%u fps\n", flags, hb,
          t_open * 1e3, w, h, nframes, fr, frd);
  CHECK(w >= 64 && w <= 4096 && h >= 64 && h <= 4096);
  CHECK(nframes > 1);
  CHECK(fr && frd);
  uint32_t sum = w32_galloc(0x80);
  bink("BinkGetSummary", 2, hb, sum);
  fprintf(stderr, "BinkGetSummary: %ux%u, total time %u ms, file rate %u/%u\n", w32_rd32(sum), w32_rd32(sum + 4),
          w32_rd32(sum + 8), w32_rd32(sum + 12), w32_rd32(sum + 16));
  CHECK(w32_rd32(sum) == w && w32_rd32(sum + 4) == h);

  uint32_t id = bink("BinkGetTrackID", 2, hb, 0);
  uint32_t tr = bink("BinkOpenTrack", 2, hb, id);
  // BINKTRACK: Frequency, Bits, Channels, MaxSize (the game sizes its buffer by MaxSize)
  uint32_t freq = tr ? w32_rd32(tr) : 0, bits = tr ? w32_rd32(tr + 4) : 0;
  uint32_t chans = tr ? w32_rd32(tr + 8) : 0, maxsz = tr ? w32_rd32(tr + 12) : 0;
  fprintf(stderr, "BinkOpenTrack(track id %u) -> 0x%x: %u Hz, %u bits, %u channel(s), max %u bytes a frame\n", id, tr,
          freq, bits, chans, maxsz);
  CHECK(tr && freq >= 8000 && bits == 16 && chans >= 1 && maxsz > 0);

  uint32_t pitch = 4 * w;
  uint32_t dst = w32_galloc(pitch * h), pcm = w32_galloc(maxsz ? maxsz : 4);
  uint8_t *prev = calloc(1, (size_t)pitch * h);
  int n = p->frames < (int)nframes ? p->frames : (int)nframes;
  int pictures = 0, changes = 0, waits = 0;
  uint64_t audio_bytes = 0;
  int peak = 0;
  double t_dec = 0;
  for (int k = 0; k < n; k++) {
    if (p->wait) while (bink("BinkWait", 1, hb)) { waits++; struct timespec s = {0, 1000000}; nanosleep(&s, 0); }
    if (k == 0) memset(RC32_H(dst), 0xab, (size_t)pitch * h);  // to see what the copy writes
    t0 = now();
    uint32_t skipped = bink("BinkDoFrame", 1, hb);
    bink("BinkCopyToBuffer", 7, hb, dst, pitch, h, 0, 0, BINKCOPYALL | BINKSURFACE32);
    t_dec += now() - t0;
    uint32_t got = bink("BinkGetTrackData", 2, tr, pcm);
    audio_bytes += got;
    for (uint32_t s = 0; s + 1 < got && got <= maxsz; s += 2) {
      int v = *(int16_t *)RC32_H(pcm + s);
      if (v < 0) v = -v;
      if (v > peak) peak = v;
    }
    // a picture: not one colour; a video: frames differ
    const uint8_t *f = RC32_H(dst);
    uint32_t lo = 0xffffffffu, hi = 0;
    for (uint32_t y = 0; y < h; y += 7)
      for (uint32_t x = 0; x < w; x += 5) {
        uint32_t px = *(const uint32_t *)(f + y * pitch + 4 * x) & 0xffffff;
        uint32_t l = (px & 0xff) + (px >> 8 & 0xff) + (px >> 16);
        if (l < lo) lo = l;
        if (l > hi) hi = l;
      }
    pictures += hi - lo > 48;
    if (k == 0) {  // what BINKSURFACE32 leaves in the fourth byte (an A8R8G8B8 texture's alpha)
      uint32_t x0 = f[3], x1 = f[(size_t)(h / 2) * pitch + 4 * (w / 2) + 3];
      fprintf(stderr, "fourth byte of each pixel: 0x%02x (corner), 0x%02x (centre)\n", x0, x1);
    }
    changes += k && memcmp(prev, f, (size_t)pitch * h) != 0;
    memcpy(prev, f, (size_t)pitch * h);
    if (verbose) fprintf(stderr, "  frame %d: DoFrame %u, luma %u..%u, %u audio bytes\n", k, skipped, lo, hi, got);
    if (p->png && k == (p->at < n ? p->at : n - 1)) {
      if (write_png(p->png, f, w, h, pitch)) fprintf(stderr, "bink_probe: cannot write %s\n", p->png);
      else fprintf(stderr, "frame %d written to %s\n", k, p->png);
    }
    bink("BinkNextFrame", 1, hb);
  }
  uint32_t at = w32_rd32(hb + 12);
  fprintf(stderr, "%d frames: %d pictures, %d changed from the one before, now at frame %u; decode + copy %.2f ms a frame\n",
          n, pictures, changes, at, n ? t_dec * 1e3 / n : 0);
  fprintf(stderr, "audio: %llu bytes of PCM (%.2f s), peak %d\n", (unsigned long long)audio_bytes,
          freq && chans ? (double)audio_bytes / (2.0 * chans * freq) : 0, peak);
  if (p->wait) fprintf(stderr, "BinkWait said wait %d times\n", waits);
  CHECK(pictures >= n - n / 8);
  CHECK(changes >= (n - 1) / 2);
  CHECK(audio_bytes > 0 && peak > 64);
  bink("BinkCloseTrack", 1, tr);
  bink("BinkClose", 1, hb);
  free(prev);
  rc32_images_fini();
  fprintf(stderr, "%s\n", fails ? "bink_probe: FAILURES" : "bink_probe: OK");
  return fails != 0;
}

static void *run_thread(void *v) {
  probe_arg *p = v;
  p->rc = run(p);
  return 0;
}

int main(int argc, char **argv) {
  probe_arg p = {0, 0, 0, 48, 24, 0, 0};
  for (int k = 1; k < argc; k++) {
    if (!strcmp(argv[k], "-v")) verbose = 1;
    else if (!strcmp(argv[k], "-wait")) p.wait = 1;
    else if (!strcmp(argv[k], "-n") && k + 1 < argc) p.frames = atoi(argv[++k]);
    else if (!strcmp(argv[k], "-at") && k + 1 < argc) p.at = atoi(argv[++k]);
    else if (!strcmp(argv[k], "-png") && k + 1 < argc) p.png = argv[++k];
    else if (!p.dll) p.dll = argv[k];
    else if (!p.video) p.video = argv[k];
  }
  if (!p.dll || !p.video) {
    fprintf(stderr, "usage: bink_probe <bink2w32.dll> <file.bk2> [-n frames] [-png out.png] [-at frame] [-wait] [-v]\n");
    return 2;
  }
  if (rc32_mem_init()) return 1;
  w32_init();
  seam_host_import = rc32_host_import;
  rc32_host_import = host_import;
  if (!rc32_image_add(&bink2w32_rc32_mod, p.dll)) { fprintf(stderr, "cannot map %s\n", p.dll); return 2; }
  if (rc32_images_bind()) { fprintf(stderr, "cannot bind the imports\n"); return 2; }
  bink_base = rc32_image_base("bink2w32.dll");
  fprintf(stderr, "bink2w32 at 0x%x, %d functions, %d imports\n", bink_base, *bink2w32_rc32_mod.table_len,
          *bink2w32_rc32_mod.imports_len);
  if (bink_base != 0x12000000u) fprintf(stderr, "note: bink2w32 is not at 0x12000000 (rebase it first)\n");
  // the static imports the seam has no shim for (a call to one would stop the game)
  int none = 0;
  for (int k = 0; k < *bink2w32_rc32_mod.imports_len; k++) {
    const char *n = bink2w32_rc32_mod.imports[k].name;
    if (w32_import(n)) continue;
    int probe_has = 0;
    for (size_t j = 0; j < sizeof extra / sizeof extra[0]; j++) probe_has |= !strcasecmp(extra[j].name, n);
    fprintf(stderr, "  no seam shim: %s%s\n", n, probe_has ? " (the probe has one)" : "");
    none++;
  }
  fprintf(stderr, "%d of Bink's imports have no shim in the seam\n", none);
  // a large host stack, as the game's thread has (translated code nests deeper than x86 code)
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 256u << 20);
  pthread_t t;
  if (pthread_create(&t, &at, run_thread, &p)) return 1;
  pthread_join(t, 0);
  return p.rc;
}
