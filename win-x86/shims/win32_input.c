// Keyboard and mouse from the host window (win-x86/app/lp_window.m) to the game: the state and
// buffered events DirectInput reads (win32_misc.c: the system keyboard and mouse), the virtual-key
// state GetAsyncKeyState / GetKeyState / GetKeyboardState report, and the window messages
// (WM_KEYDOWN / WM_KEYUP / WM_CHAR, WM_MOUSEMOVE and the buttons, win32_user32.c's queue).
//
// The host calls in with macOS virtual key codes; each maps to a DirectInput scan code (DIK_*)
// and a Windows virtual key. LP_INPUT_SCRIPT plays keys for unattended runs (w32_input_script).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "win32_shims.h"

// macOS key code -> DIK, VK (0: not mapped)
static const uint8_t mac_keys[128][2] = {
    [0x00] = {0x1e, 'A'}, [0x01] = {0x1f, 'S'}, [0x02] = {0x20, 'D'}, [0x03] = {0x21, 'F'},
    [0x04] = {0x23, 'H'}, [0x05] = {0x22, 'G'}, [0x06] = {0x2c, 'Z'}, [0x07] = {0x2d, 'X'},
    [0x08] = {0x2e, 'C'}, [0x09] = {0x2f, 'V'}, [0x0b] = {0x30, 'B'}, [0x0c] = {0x10, 'Q'},
    [0x0d] = {0x11, 'W'}, [0x0e] = {0x12, 'E'}, [0x0f] = {0x13, 'R'}, [0x10] = {0x15, 'Y'},
    [0x11] = {0x14, 'T'}, [0x12] = {0x02, '1'}, [0x13] = {0x03, '2'}, [0x14] = {0x04, '3'},
    [0x15] = {0x05, '4'}, [0x16] = {0x07, '6'}, [0x17] = {0x06, '5'}, [0x18] = {0x0d, 0xbb},
    [0x19] = {0x0a, '9'}, [0x1a] = {0x08, '7'}, [0x1b] = {0x0c, 0xbd}, [0x1c] = {0x09, '8'},
    [0x1d] = {0x0b, '0'}, [0x1e] = {0x1b, 0xdd}, [0x1f] = {0x18, 'O'}, [0x20] = {0x16, 'U'},
    [0x21] = {0x1a, 0xdb}, [0x22] = {0x17, 'I'}, [0x23] = {0x19, 'P'}, [0x24] = {0x1c, 0x0d},
    [0x25] = {0x26, 'L'}, [0x26] = {0x24, 'J'}, [0x27] = {0x28, 0xde}, [0x28] = {0x25, 'K'},
    [0x29] = {0x27, 0xba}, [0x2a] = {0x2b, 0xdc}, [0x2b] = {0x33, 0xbc}, [0x2c] = {0x35, 0xbf},
    [0x2d] = {0x31, 'N'}, [0x2e] = {0x32, 'M'}, [0x2f] = {0x34, 0xbe}, [0x30] = {0x0f, 0x09},
    [0x31] = {0x39, 0x20}, [0x32] = {0x29, 0xc0}, [0x33] = {0x0e, 0x08}, [0x35] = {0x01, 0x1b},
    [0x37] = {0xdb, 0x5b}, [0x38] = {0x2a, 0x10}, [0x39] = {0x3a, 0x14}, [0x3a] = {0x38, 0x12},
    [0x3b] = {0x1d, 0x11}, [0x3c] = {0x36, 0x10}, [0x3d] = {0xb8, 0x12}, [0x3e] = {0x9d, 0x11},
    [0x36] = {0xdc, 0x5c}, [0x41] = {0x53, 0x6e}, [0x43] = {0x37, 0x6a}, [0x45] = {0x4e, 0x6b},
    [0x4b] = {0xb5, 0x6f}, [0x4c] = {0x9c, 0x0d}, [0x4e] = {0x4a, 0x6d}, [0x52] = {0x52, 0x60},
    [0x53] = {0x4f, 0x61}, [0x54] = {0x50, 0x62}, [0x55] = {0x51, 0x63}, [0x56] = {0x4b, 0x64},
    [0x57] = {0x4c, 0x65}, [0x58] = {0x4d, 0x66}, [0x59] = {0x47, 0x67}, [0x5b] = {0x48, 0x68},
    [0x5c] = {0x49, 0x69}, [0x60] = {0x3f, 0x74}, [0x61] = {0x40, 0x75}, [0x62] = {0x41, 0x76},
    [0x63] = {0x3d, 0x72}, [0x64] = {0x42, 0x77}, [0x65] = {0x43, 0x78}, [0x67] = {0x57, 0x7a},
    [0x6d] = {0x44, 0x79}, [0x6f] = {0x58, 0x7b}, [0x73] = {0xc7, 0x24}, [0x74] = {0xc9, 0x21},
    [0x75] = {0xd3, 0x2e}, [0x76] = {0x3e, 0x73}, [0x77] = {0xcf, 0x23}, [0x78] = {0x3c, 0x71},
    [0x79] = {0xd1, 0x22}, [0x7a] = {0x3b, 0x70}, [0x7b] = {0xcb, 0x25}, [0x7c] = {0xcd, 0x27},
    [0x7d] = {0xd0, 0x28}, [0x7e] = {0xc8, 0x26},
};

#define NEV 256
typedef struct { uint32_t ofs, data, time, seq; } di_event;
static struct {
  uint8_t dik[256];  // DirectInput keyboard state: 0x80 down
  uint8_t vk[256];   // virtual keys: 0x80 down, 0x01 toggled
  di_event kev[NEV], mev[NEV];
  uint32_t kh, kt, mh, mt, seq;
  int32_t mdx, mdy, mdz;  // relative motion since the last GetDeviceState
  uint8_t mbtn[8];
  int32_t mx, my;         // the pointer in client pixels
} in;
static pthread_mutex_t in_mu = PTHREAD_MUTEX_INITIALIZER;

static uint32_t now_ms(void) { return (uint32_t)(clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000); }

static void push_ev(di_event *r, uint32_t *t, uint32_t h, uint32_t ofs, uint32_t data) {
  if ((*t + 1) % NEV == h) return;  // full: dropped (DI_BUFFEROVERFLOW in DirectInput)
  r[*t] = (di_event){ofs, data, now_ms(), ++in.seq};
  *t = (*t + 1) % NEV;
}

void w32_post_input(uint32_t msg, uint32_t wp, uint32_t lp);  // win32_user32.c

// A key from the host: macOS key code, down or up, and the character it types (0: none).
void w32_input_key(int mac, int down, uint32_t ch) {
  if (mac < 0 || mac >= 128 || !mac_keys[mac][0]) return;
  uint8_t dik = mac_keys[mac][0], vk = mac_keys[mac][1];
  pthread_mutex_lock(&in_mu);
  int was = in.dik[dik] & 0x80;
  in.dik[dik] = down ? 0x80 : 0;
  if (down && !(in.vk[vk] & 0x80)) in.vk[vk] ^= 1;
  in.vk[vk] = (uint8_t)((in.vk[vk] & 1) | (down ? 0x80 : 0));
  if (!was != !down) push_ev(in.kev, &in.kt, in.kh, dik, down ? 0x80 : 0);
  pthread_mutex_unlock(&in_mu);
  uint32_t lp = 1 | (uint32_t)(dik & 0x7f) << 16 | (dik & 0x80 ? 1u << 24 : 0) | (down ? (was ? 1u << 30 : 0) : 3u << 30);
  w32_post_input(down ? 0x100 : 0x101, vk, lp);  // WM_KEYDOWN / WM_KEYUP
  if (down && ch && ch < 0xf700) w32_post_input(0x102, ch, lp);  // WM_CHAR (not the function keys' private-use codes)
}

// The window lost the keyboard: every key and button held goes up, as nothing tells the game later
// (a key released in another application never reaches this one).
void w32_input_release_all(void) {
  for (int mac = 0; mac < 128; mac++) {
    pthread_mutex_lock(&in_mu);
    int down = mac_keys[mac][0] && (in.dik[mac_keys[mac][0]] & 0x80);
    pthread_mutex_unlock(&in_mu);
    if (down) w32_input_key(mac, 0, 0);
  }
  for (int b = 0; b < 3; b++) {
    pthread_mutex_lock(&in_mu);
    int down = in.mbtn[b] & 0x80;
    pthread_mutex_unlock(&in_mu);
    if (down) w32_input_mouse_button(b, 0);
  }
}

// The pointer from the host: position in client pixels, relative motion, wheel steps (120 each)
void w32_input_mouse_move(int32_t x, int32_t y, int32_t dx, int32_t dy) {
  pthread_mutex_lock(&in_mu);
  in.mx = x;
  in.my = y;
  in.mdx += dx;
  in.mdy += dy;
  if (dx) push_ev(in.mev, &in.mt, in.mh, 0, (uint32_t)dx);  // DIMOFS_X
  if (dy) push_ev(in.mev, &in.mt, in.mh, 4, (uint32_t)dy);  // DIMOFS_Y
  pthread_mutex_unlock(&in_mu);
  w32_post_input(0x200, 0, (uint32_t)(x & 0xffff) | (uint32_t)y << 16);  // WM_MOUSEMOVE
}
void w32_input_mouse_wheel(int32_t delta) {
  pthread_mutex_lock(&in_mu);
  in.mdz += delta;
  push_ev(in.mev, &in.mt, in.mh, 8, (uint32_t)delta);  // DIMOFS_Z
  int32_t x = in.mx, y = in.my;
  pthread_mutex_unlock(&in_mu);
  w32_post_input(0x20a, (uint32_t)(delta & 0xffff) << 16, (uint32_t)(x & 0xffff) | (uint32_t)y << 16);
}
// button 0 left, 1 right, 2 middle
void w32_input_mouse_button(int b, int down) {
  static const uint8_t vk[3] = {0x01, 0x02, 0x04};
  static const uint32_t msg[3] = {0x201, 0x204, 0x207};
  if (b < 0 || b > 2) return;
  pthread_mutex_lock(&in_mu);
  in.mbtn[b] = down ? 0x80 : 0;
  in.vk[vk[b]] = down ? 0x80 : 0;
  push_ev(in.mev, &in.mt, in.mh, 12 + (uint32_t)b, down ? 0x80 : 0);  // DIMOFS_BUTTON0 + b
  int32_t x = in.mx, y = in.my;
  pthread_mutex_unlock(&in_mu);
  w32_post_input(msg[b] + (down ? 0 : 1), 0, (uint32_t)(x & 0xffff) | (uint32_t)y << 16);
}

// --- what the game reads -----------------------------------------------------------------------
// DirectInput GetDeviceState: the keyboard's 256 bytes, the mouse's DIMOUSESTATE(2) (relative
// axes since the last read, then the buttons)
void w32_input_keyboard_state(uint32_t g, uint32_t cb) {
  pthread_mutex_lock(&in_mu);
  memcpy(RC32_H(g), in.dik, cb < 256 ? cb : 256);
  pthread_mutex_unlock(&in_mu);
}
void w32_input_mouse_state(uint32_t g, uint32_t cb) {
  uint8_t s[20] = {0};
  pthread_mutex_lock(&in_mu);
  memcpy(s, &in.mdx, 4);
  memcpy(s + 4, &in.mdy, 4);
  memcpy(s + 8, &in.mdz, 4);
  memcpy(s + 12, in.mbtn, 8);
  in.mdx = in.mdy = in.mdz = 0;
  pthread_mutex_unlock(&in_mu);
  memcpy(RC32_H(g), s, cb < 20 ? cb : 20);
}
// DirectInput GetDeviceData: up to *n DIDEVICEOBJECTDATA of cb bytes each (16, or 20 with
// uAppData) from the device's buffer; dst 0 counts (with DIGDD_PEEK, flags 1, nothing is taken)
uint32_t w32_input_events(int mouse, uint32_t dst, uint32_t cb, uint32_t n, uint32_t flags) {
  pthread_mutex_lock(&in_mu);
  di_event *r = mouse ? in.mev : in.kev;
  uint32_t *h = mouse ? &in.mh : &in.kh, t = mouse ? in.mt : in.kt, k = 0, p = *h;
  for (; p != t && k < n; p = (p + 1) % NEV, k++) {
    if (!dst) continue;
    uint32_t e[5] = {r[p].ofs, r[p].data, r[p].time, r[p].seq, 0};
    memcpy(RC32_H(dst + k * cb), e, cb < 20 ? cb : 20);
  }
  if (!(flags & 1)) *h = p;
  pthread_mutex_unlock(&in_mu);
  return k;
}
// GetAsyncKeyState (0x8000 down), GetKeyState (0x8000 down, 1 toggled), GetKeyboardState
uint32_t w32_input_vk(uint32_t vk, int async) {
  if (vk > 255) return 0;
  pthread_mutex_lock(&in_mu);
  uint8_t v = in.vk[vk];
  if (vk == 0x10 || vk == 0x11 || vk == 0x12) {  // the generic shift / control / alt: either side
    static const uint8_t dl[3] = {0x2a, 0x1d, 0x38}, dr[3] = {0x36, 0x9d, 0xb8};
    if ((in.dik[dl[vk - 0x10]] | in.dik[dr[vk - 0x10]]) & 0x80) v |= 0x80;
  }
  pthread_mutex_unlock(&in_mu);
  return async ? (v & 0x80 ? 0x8000u : 0) : (v & 0x80 ? 0xff80u : 0) | (v & 1);
}
void w32_input_keyboard_vk(uint32_t g) {
  pthread_mutex_lock(&in_mu);
  memcpy(RC32_H(g), in.vk, 256);
  pthread_mutex_unlock(&in_mu);
}
void w32_input_cursor(int32_t *x, int32_t *y) {
  pthread_mutex_lock(&in_mu);
  *x = in.mx;
  *y = in.my;
  pthread_mutex_unlock(&in_mu);
}

// --- LP_INPUT_SCRIPT: keys for unattended runs ------------------------------------------------
// Steps separated by commas:
//   SECONDS:KEY  press KEY (a macOS key code in hex: 24 Return, 35 Escape, 75 forward delete,
//                7e Up, ...) for 100 ms, SECONDS after the window appeared or the last wait ended
//   @TEXT        wait until TEXT appears in the process's log (stderr) from then on, at most
//                LP_INPUT_WAIT seconds (default 120); the clock of the next steps starts there
// The log is read through a pipe the script puts in front of stderr; what it reads goes on to
// the original stderr unchanged.
static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_cv = PTHREAD_COND_INITIALIZER;
static char log_tail[8192];  // the last bytes of the log, NUL-terminated
static uint64_t log_gen;     // bumped with every read
static uint64_t log_total;   // bytes read so far (the tail ends there)

static void *log_pump(void *arg) {
  int in = ((int *)arg)[0], out = ((int *)arg)[1];
  char b[4096];
  ssize_t n;
  while ((n = read(in, b, sizeof b)) > 0) {
    for (ssize_t o = 0; o < n;) {
      ssize_t w = write(out, b + o, (size_t)(n - o));
      if (w <= 0) break;
      o += w;
    }
    pthread_mutex_lock(&log_mu);
    size_t have = strlen(log_tail), add = (size_t)n < sizeof log_tail - 1 ? (size_t)n : sizeof log_tail - 1;
    if (have + add > sizeof log_tail - 1) {
      size_t drop = have + add - (sizeof log_tail - 1);
      memmove(log_tail, log_tail + drop, have - drop + 1);
      have -= drop;
    }
    memcpy(log_tail + have, b + (size_t)n - add, add);
    log_tail[have + add] = 0;
    for (size_t k = have; k < have + add; k++)
      if (!log_tail[k]) log_tail[k] = ' ';
    log_gen++;
    log_total += (uint64_t)n;
    pthread_cond_broadcast(&log_cv);
    pthread_mutex_unlock(&log_mu);
  }
  return 0;
}

// wait for text in what the log gets from now on; 0 on timeout
static int log_wait(const char *text, double secs) {
  struct timespec end;
  clock_gettime(CLOCK_REALTIME, &end);
  end.tv_sec += (time_t)secs;
  pthread_mutex_lock(&log_mu);
  uint64_t start = log_total;
  int found = 0;
  for (;;) {
    size_t len = strlen(log_tail), since = (size_t)(log_total - start);
    if (since > len) since = len;
    if (strstr(log_tail + len - since, text)) { found = 1; break; }
    uint64_t g = log_gen;
    if (pthread_cond_timedwait(&log_cv, &log_mu, &end) && g == log_gen) break;
  }
  pthread_mutex_unlock(&log_mu);
  return found;
}

// seconds since the script started, for matching the log with screenshots
static double script_t0;
static double script_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (double)t.tv_sec + t.tv_nsec * 1e-9 - script_t0;
}

static void *script(void *arg) {
  script_t0 = script_now();
  {
    time_t now = time(0);
    char hms[16];
    strftime(hms, sizeof hms, "%H:%M:%S", localtime(&now));
    fprintf(stderr, "w32: input script: [0.0] starts at %s\n", hms);
  }
  char *s = arg;
  const char *w = getenv("LP_INPUT_WAIT");
  double t0 = 0, wait_max = w && atof(w) > 0 ? atof(w) : 120;
  char *save = 0;
  for (char *p = strtok_r(s, ",", &save); p; p = strtok_r(0, ",", &save)) {
    double at;
    unsigned key;
    if (*p == '@') {
      int ok = log_wait(p + 1, wait_max);
      fprintf(stderr, "w32: input script: [%.1f] %s \"%s\"\n", script_now(), ok ? "saw" : "gave up waiting for", p + 1);
      t0 = 0;
      continue;
    }
    if (sscanf(p, "%lf:%x", &at, &key) != 2) continue;
    if (at > t0) usleep((useconds_t)((at - t0) * 1e6));
    t0 = at;
    fprintf(stderr, "w32: input script: [%.1f] key 0x%x at %.1f s\n", script_now(), key, at);
    w32_input_key((int)key, 1, key == 0x24 ? '\r' : 0);
    usleep(100000);
    w32_input_key((int)key, 0, 0);
    t0 += 0.1;
  }
  free(s);
  return 0;
}
void w32_input_script(void) {
  static int started;
  static int fds[2];
  const char *e = getenv("LP_INPUT_SCRIPT");
  if (started || !e || !*e) return;
  started = 1;
  pthread_t th;
  int p[2];
  if (strchr(e, '@') && !pipe(p)) {  // the log, for the waits
    fds[0] = p[0];
    fds[1] = dup(2);
    dup2(p[1], 2);
    close(p[1]);
    if (!pthread_create(&th, 0, log_pump, fds)) pthread_detach(th);
  }
  if (!pthread_create(&th, 0, script, strdup(e))) pthread_detach(th);
}
