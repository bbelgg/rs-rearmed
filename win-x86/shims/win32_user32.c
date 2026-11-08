// user32: window classes and windows as records the guest's window procedures see (the picture
// itself goes through the D3D9 layer, d3d9_platform_context), a posted-message queue, the
// display, cursor, keyboard, clipboard and message boxes.
//
// A window procedure is guest code: CreateWindowEx sends it WM_NCCREATE and WM_CREATE,
// DispatchMessage, SendMessage and CallWindowProc call it (stdcall, 4 words).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "win32_shims.h"

#define MAXCLS 64
#define MAXWND 256
#define HWND_BASE 0x00030000u  // window handles: HWND_BASE + 4 * index (never 0)
typedef struct { char name[128]; uint32_t proc, style, extra, inst, atom; int wide; } wclass;
typedef struct {
  int used;
  uint32_t proc, style, exstyle, parent, userdata, id, inst;
  int32_t x, y, w, h;
  int visible, wide;
  char title[256];
} wnd;
static wclass cls[MAXCLS];
static int ncls;
static wnd wnds[MAXWND];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

// The host's main screen (win-x86/app/lp_window.m); without a window seam (the tests), 1920x1080.
__attribute__((weak)) void lp_screen_size(int *w, int *h) {
  *w = 1920;
  *h = 1080;
}
void w32_display_size(int32_t *w, int32_t *h) {
  int sw, sh;
  lp_screen_size(&sw, &sh);
  *w = sw;
  *h = sh;
  const char *e = getenv("RS_D3D9_DISPLAY");
  unsigned a, b;
  if (e && sscanf(e, "%ux%u", &a, &b) == 2 && a && b) { *w = (int32_t)a; *h = (int32_t)b; }
}
#define display_size w32_display_size

// The game set its top-level window's style (win-x86/app/lp_window.m: the window mode follows it).
__attribute__((weak)) void lp_window_style(uint32_t hwnd, uint32_t style) {
  (void)hwnd;
  (void)style;
}

static wnd *wnd_of(uint32_t h) {
  uint32_t k = (h - HWND_BASE) / 4;
  return h >= HWND_BASE && !(h & 3) && k < MAXWND && wnds[k].used ? &wnds[k] : 0;
}
static uint32_t desktop(void) { return HWND_BASE + 4 * (MAXWND - 1); }


// a class name argument: a string or an atom (MAKEINTATOM)
static wclass *class_named(uint32_t g, int wide) {
  char n[128];
  if (g < 0x10000) {
    for (int k = 0; k < ncls; k++)
      if (cls[k].atom == g) return &cls[k];
    return 0;
  }
  if (wide) w32_wstr(g, n, sizeof n);
  else snprintf(n, sizeof n, "%s", (const char *)RC32_H(g));
  for (int k = 0; k < ncls; k++)
    if (!strcasecmp(cls[k].name, n)) return &cls[k];
  return 0;
}

static uint32_t register_class(rc_ctx *c, uint32_t style, uint32_t proc, uint32_t extra, uint32_t inst, uint32_t name, int wide) {
  pthread_mutex_lock(&mu);
  uint32_t atom = 0;
  if (class_named(name, wide)) {
    w32_set_error(c, W32_ERROR_CLASS_ALREADY_EXISTS);
  } else if (ncls < MAXCLS) {
    wclass *k = &cls[ncls];
    if (wide) w32_wstr(name, k->name, sizeof k->name);
    else snprintf(k->name, sizeof k->name, "%s", (const char *)RC32_H(name));
    k->proc = proc;
    k->style = style;
    k->extra = extra;
    k->inst = inst;
    k->wide = wide;
    k->atom = atom = 0xc000u + (uint32_t)ncls++;
    if (w32_log) fprintf(stderr, "w32: RegisterClass(\"%s\", proc 0x%x) -> 0x%x\n", k->name, proc, atom);
  }
  pthread_mutex_unlock(&mu);
  return atom;
}
// WNDCLASS: style, proc, cbClsExtra, cbWndExtra, hInstance, hIcon, hCursor, hbr, menu, name
static void u_RegisterClassA(rc_ctx *c) { uint32_t p = w32_arg(c, 0); w32_ret(c, register_class(c, w32_rd32(p), w32_rd32(p + 4), w32_rd32(p + 12), w32_rd32(p + 16), w32_rd32(p + 36), 0), 1); }
static void u_RegisterClassW(rc_ctx *c) { uint32_t p = w32_arg(c, 0); w32_ret(c, register_class(c, w32_rd32(p), w32_rd32(p + 4), w32_rd32(p + 12), w32_rd32(p + 16), w32_rd32(p + 36), 1), 1); }
// WNDCLASSEX: cbSize first
static void u_RegisterClassExA(rc_ctx *c) { uint32_t p = w32_arg(c, 0) + 4; w32_ret(c, register_class(c, w32_rd32(p), w32_rd32(p + 4), w32_rd32(p + 12), w32_rd32(p + 16), w32_rd32(p + 36), 0), 1); }
static void u_RegisterClassExW(rc_ctx *c) { uint32_t p = w32_arg(c, 0) + 4; w32_ret(c, register_class(c, w32_rd32(p), w32_rd32(p + 4), w32_rd32(p + 12), w32_rd32(p + 16), w32_rd32(p + 36), 1), 1); }
static void u_UnregisterClassA(rc_ctx *c) { w32_ret(c, 1, 2); }
static void u_UnregisterClassW(rc_ctx *c) { w32_ret(c, 1, 2); }

static uint32_t call_proc(rc_ctx *c, uint32_t proc, uint32_t h, uint32_t msg, uint32_t wp, uint32_t lp) {
  if (!proc) return 0;
  uint32_t a[4] = {h, msg, wp, lp};
  return w32_guest_call(c, proc, W32_STDCALL, 0, a, 4, 0);
}

static uint32_t create_window(rc_ctx *c, int wide) {
  uint32_t ex = w32_arg(c, 0), cname = w32_arg(c, 1), title = w32_arg(c, 2), style = w32_arg(c, 3);
  int32_t x = (int32_t)w32_arg(c, 4), y = (int32_t)w32_arg(c, 5), w = (int32_t)w32_arg(c, 6), h = (int32_t)w32_arg(c, 7);
  uint32_t parent = w32_arg(c, 8), menu = w32_arg(c, 9), inst = w32_arg(c, 10), param = w32_arg(c, 11);
  pthread_mutex_lock(&mu);
  wclass *k = class_named(cname, wide);
  int i = 0;
  while (i < MAXWND - 1 && wnds[i].used) i++;
  if (!k || i == MAXWND - 1) {
    pthread_mutex_unlock(&mu);
    w32_set_error(c, k ? W32_ERROR_NOT_ENOUGH_MEMORY : 1411);  // ERROR_CANNOT_FIND_WND_CLASS
    fprintf(stderr, "w32: CreateWindowEx: %s\n", k ? "too many windows" : "no such class");
    return 0;
  }
  int32_t dw, dh;
  display_size(&dw, &dh);
  wnd *o = &wnds[i];
  memset(o, 0, sizeof *o);
  o->used = 1;
  o->proc = k->proc;
  o->style = style;
  o->exstyle = ex;
  o->parent = parent;
  o->id = menu;
  o->inst = inst;
  o->wide = wide;
  o->x = x == (int32_t)0x80000000 ? 0 : x;
  o->y = y == (int32_t)0x80000000 ? 0 : y;
  o->w = w == (int32_t)0x80000000 ? dw : w;
  o->h = h == (int32_t)0x80000000 ? dh : h;
  o->visible = (style & 0x10000000u) != 0;  // WS_VISIBLE
  if (title) {
    if (wide) w32_wstr(title, o->title, sizeof o->title);
    else snprintf(o->title, sizeof o->title, "%s", (const char *)RC32_H(title));
  }
  uint32_t hw = HWND_BASE + 4 * (uint32_t)i;
  pthread_mutex_unlock(&mu);
  fprintf(stderr, "w32: CreateWindowEx(\"%s\", \"%s\", style 0x%x, %d,%d %dx%d) -> 0x%x\n", k->name, o->title, style,
          o->x, o->y, o->w, o->h, hw);
  // CREATESTRUCT: param, inst, menu, parent, cy, cx, y, x, style, name, class, exstyle
  uint32_t cs = w32_galloc(48);
  uint32_t v[12] = {param, inst, menu, parent, (uint32_t)o->h, (uint32_t)o->w, (uint32_t)o->y, (uint32_t)o->x, style, title, cname, ex};
  memcpy(RC32_H(cs), v, 48);
  if (!call_proc(c, o->proc, hw, 0x81, 0, cs)) {  // WM_NCCREATE
    fprintf(stderr, "w32: CreateWindowEx: WM_NCCREATE refused\n");
    o->used = 0;
    w32_gfree(cs, 48);
    return 0;
  }
  if ((int32_t)call_proc(c, o->proc, hw, 0x01, 0, cs) == -1) {  // WM_CREATE
    fprintf(stderr, "w32: CreateWindowEx: WM_CREATE refused\n");
    o->used = 0;
    w32_gfree(cs, 48);
    return 0;
  }
  w32_gfree(cs, 48);
  call_proc(c, o->proc, hw, 0x05, 0, (uint32_t)(o->w & 0xffff) | (uint32_t)o->h << 16);  // WM_SIZE
  return hw;
}
static void u_CreateWindowExA(rc_ctx *c) { uint32_t h = create_window(c, 0); w32_ret(c, h, 12); }
static void u_CreateWindowExW(rc_ctx *c) { uint32_t h = create_window(c, 1); w32_ret(c, h, 12); }
static int destroy_window(rc_ctx *c, uint32_t h) {
  wnd *o = wnd_of(h);
  if (o) {
    call_proc(c, o->proc, h, 0x02, 0, 0);  // WM_DESTROY
    o->used = 0;
  }
  return o != 0;
}
static void u_DestroyWindow(rc_ctx *c) { w32_ret(c, (uint32_t)destroy_window(c, w32_arg(c, 0)), 1); }
static void def_proc(rc_ctx *c) {
  uint32_t msg = w32_arg(c, 1);
  if (msg == 0x10) destroy_window(c, w32_arg(c, 0));  // WM_CLOSE: the game's WM_DESTROY posts WM_QUIT
  w32_ret(c, msg == 0x81, 4);                         // WM_NCCREATE: TRUE
}
static void u_DefWindowProcA(rc_ctx *c) { def_proc(c); }
static void u_DefWindowProcW(rc_ctx *c) { def_proc(c); }
static void u_CallWindowProcW(rc_ctx *c) {
  w32_ret(c, call_proc(c, w32_arg(c, 0), w32_arg(c, 1), w32_arg(c, 2), w32_arg(c, 3), w32_arg(c, 4)), 5);
}
static void u_CallWindowProcA(rc_ctx *c) { u_CallWindowProcW(c); }
static void send_message(rc_ctx *c) {
  wnd *o = wnd_of(w32_arg(c, 0));
  w32_ret(c, o ? call_proc(c, o->proc, w32_arg(c, 0), w32_arg(c, 1), w32_arg(c, 2), w32_arg(c, 3)) : 0, 4);
}
static void u_SendMessageA(rc_ctx *c) { send_message(c); }
static void u_SendMessageW(rc_ctx *c) { send_message(c); }

static uint32_t window_long(wnd *o, int32_t i) {
  switch (i) {
  case -4: return o->proc;     // GWL_WNDPROC
  case -6: return o->inst;     // GWL_HINSTANCE
  case -8: return o->parent;   // GWL_HWNDPARENT
  case -12: return o->id;      // GWL_ID
  case -16: return o->style | (o->visible ? 0x10000000u : 0);
  case -20: return o->exstyle;
  case -21: return o->userdata;
  }
  return 0;
}
static uint32_t set_window_long(wnd *o, int32_t i, uint32_t v) {
  uint32_t old = window_long(o, i);
  switch (i) {
  case -4: o->proc = v; break;
  case -16: o->style = v & ~0x10000000u; o->visible = (v & 0x10000000u) != 0; break;
  case -20: o->exstyle = v; break;
  case -21: o->userdata = v; break;
  case -12: o->id = v; break;
  }
  return old;
}
static void u_GetWindowLongA(rc_ctx *c) { wnd *o = wnd_of(w32_arg(c, 0)); w32_ret(c, o ? window_long(o, (int32_t)w32_arg(c, 1)) : 0, 2); }
static void u_GetWindowLongW(rc_ctx *c) { u_GetWindowLongA(c); }
static void u_SetWindowLongA(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  wnd *o = wnd_of(h);
  int32_t i = (int32_t)w32_arg(c, 1);
  uint32_t v = w32_arg(c, 2);
  if (w32_log && (i == -16 || i == -20)) fprintf(stderr, "w32: SetWindowLong(0x%x, %d, 0x%x)\n", h, i, v);
  w32_ret(c, o ? set_window_long(o, i, v) : 0, 3);  // (pops the arguments)
  if (o && i == -16 && !o->parent) lp_window_style(h, v);  // the game's window mode
}
static void u_SetWindowLongW(rc_ctx *c) { u_SetWindowLongA(c); }
static void u_ShowWindow(rc_ctx *c) {
  wnd *o = wnd_of(w32_arg(c, 0));
  int was = o && o->visible;
  if (o) o->visible = w32_arg(c, 1) != 0;  // SW_HIDE is 0
  w32_ret(c, (uint32_t)was, 2);
}
static void u_UpdateWindow(rc_ctx *c) { w32_ret(c, wnd_of(w32_arg(c, 0)) != 0, 1); }
static void u_EnableWindow(rc_ctx *c) { w32_ret(c, 0, 2); }
static void u_SetWindowPos(rc_ctx *c) {
  wnd *o = wnd_of(w32_arg(c, 0));
  uint32_t fl = w32_arg(c, 6);
  if (w32_log && !(fl & 1)) fprintf(stderr, "w32: SetWindowPos(0x%x, after 0x%x, %d,%d %dx%d, flags 0x%x)\n", w32_arg(c, 0), w32_arg(c, 1),
          (int32_t)w32_arg(c, 2), (int32_t)w32_arg(c, 3), (int32_t)w32_arg(c, 4), (int32_t)w32_arg(c, 5), fl);
  if (o) {
    if (!(fl & 2)) { o->x = (int32_t)w32_arg(c, 2); o->y = (int32_t)w32_arg(c, 3); }  // SWP_NOMOVE
    if (!(fl & 1)) { o->w = (int32_t)w32_arg(c, 4); o->h = (int32_t)w32_arg(c, 5); }  // SWP_NOSIZE
    if (fl & 0x40) o->visible = 1;                                                    // SWP_SHOWWINDOW
    if (fl & 0x80) o->visible = 0;                                                    // SWP_HIDEWINDOW
  }
  w32_ret(c, o != 0, 7);
}
static void u_MoveWindow(rc_ctx *c) {
  wnd *o = wnd_of(w32_arg(c, 0));
  if (w32_log) fprintf(stderr, "w32: MoveWindow(0x%x, %d,%d %dx%d)\n", w32_arg(c, 0), (int32_t)w32_arg(c, 1), (int32_t)w32_arg(c, 2),
          (int32_t)w32_arg(c, 3), (int32_t)w32_arg(c, 4));
  if (o) { o->x = (int32_t)w32_arg(c, 1); o->y = (int32_t)w32_arg(c, 2); o->w = (int32_t)w32_arg(c, 3); o->h = (int32_t)w32_arg(c, 4); }
  w32_ret(c, o != 0, 6);
}
static void rect(uint32_t r, int32_t l, int32_t t, int32_t rr, int32_t b) {
  int32_t v[4] = {l, t, rr, b};
  memcpy(RC32_H(r), v, 16);
}
static int wnd_rect(uint32_t h, int32_t *x, int32_t *y, int32_t *w, int32_t *ht) {
  wnd *o = wnd_of(h);
  if (o) { *x = o->x; *y = o->y; *w = o->w; *ht = o->h; return 1; }
  if (h == desktop() || !h) { *x = *y = 0; display_size(w, ht); return 1; }
  return 0;
}
// the windows have no frame here: client area = window area
static void u_GetClientRect(rc_ctx *c) {
  int32_t x, y, w, h;
  int ok = wnd_rect(w32_arg(c, 0), &x, &y, &w, &h);
  if (ok) rect(w32_arg(c, 1), 0, 0, w, h);
  w32_ret(c, ok, 2);
}
static void u_GetWindowRect(rc_ctx *c) {
  int32_t x, y, w, h;
  int ok = wnd_rect(w32_arg(c, 0), &x, &y, &w, &h);
  if (ok) rect(w32_arg(c, 1), x, y, x + w, y + h);
  w32_ret(c, ok, 2);
}
static void u_GetWindowInfo(rc_ctx *c) {
  int32_t x, y, w, h;
  uint32_t h0 = w32_arg(c, 0), p = w32_arg(c, 1);
  int ok = wnd_rect(h0, &x, &y, &w, &h);
  if (ok) {
    wnd *o = wnd_of(h0);
    memset(RC32_H(p + 4), 0, 56);
    rect(p + 4, x, y, x + w, y + h);
    rect(p + 20, x, y, x + w, y + h);
    w32_wr32(p + 36, o ? window_long(o, -16) : 0);
    w32_wr32(p + 40, o ? o->exstyle : 0);
    w32_wr32(p + 44, 1);  // WS_ACTIVECAPTION
  }
  w32_ret(c, ok, 2);
}
static void u_AdjustWindowRect(rc_ctx *c) { w32_ret(c, 1, 3); }
static void u_AdjustWindowRectEx(rc_ctx *c) { w32_ret(c, 1, 4); }
static void u_ClientToScreen(rc_ctx *c) { w32_ret(c, 1, 2); }
static void u_ScreenToClient(rc_ctx *c) { w32_ret(c, 1, 2); }
static uint32_t focus;
static uint32_t first_window(void) {
  for (int k = 0; k < MAXWND - 1; k++)
    if (wnds[k].used && !wnds[k].parent) return HWND_BASE + 4 * (uint32_t)k;
  return 0;
}
static void u_GetFocus(rc_ctx *c) { w32_ret(c, focus ? focus : first_window(), 0); }
static void u_SetFocus(rc_ctx *c) { uint32_t o = focus; focus = w32_arg(c, 0); w32_ret(c, o, 1); }
static void u_GetForegroundWindow(rc_ctx *c) { w32_ret(c, first_window(), 0); }
static void u_SetForegroundWindow(rc_ctx *c) { w32_ret(c, 1, 1); }
static void u_GetActiveWindow(rc_ctx *c) { w32_ret(c, first_window(), 0); }
static void u_GetDesktopWindow(rc_ctx *c) { w32_ret(c, desktop(), 0); }
static void u_IsWindow(rc_ctx *c) { w32_ret(c, wnd_of(w32_arg(c, 0)) != 0, 1); }
static void u_IsWindowVisible(rc_ctx *c) { wnd *o = wnd_of(w32_arg(c, 0)); w32_ret(c, o && o->visible, 1); }
static void u_IsIconic(rc_ctx *c) { w32_ret(c, 0, 1); }
static void u_GetDC(rc_ctx *c) { w32_ret(c, 0x00dc0000u | (w32_arg(c, 0) & 0xffff), 1); }
static void u_ReleaseDC(rc_ctx *c) { w32_ret(c, 1, 2); }
static void u_SetWindowTextA(rc_ctx *c) {
  wnd *o = wnd_of(w32_arg(c, 0));
  if (o) snprintf(o->title, sizeof o->title, "%s", w32_cstr(w32_arg(c, 1)) ? w32_cstr(w32_arg(c, 1)) : "");
  w32_ret(c, o != 0, 2);
}
static void u_SetWindowTextW(rc_ctx *c) {
  wnd *o = wnd_of(w32_arg(c, 0));
  if (o) w32_wstr(w32_arg(c, 1), o->title, sizeof o->title);
  w32_ret(c, o != 0, 2);
}

// --- the display ---------------------------------------------------------------------------
static void u_GetSystemMetrics(rc_ctx *c) {
  int32_t w, h;
  display_size(&w, &h);
  uint32_t i = w32_arg(c, 0), v = 0;
  switch (i) {
  case 0: case 16: case 78: v = (uint32_t)w; break;  // SM_CXSCREEN, SM_CXFULLSCREEN, SM_CXVIRTUALSCREEN
  case 1: case 17: case 79: v = (uint32_t)h; break;
  case 2: case 3: v = 17; break;                     // scroll bar sizes
  case 4: v = 23; break;                             // SM_CYCAPTION
  case 5: case 6: v = 1; break;                      // borders
  case 7: case 8: v = 3; break;                      // fixed frames
  case 11: case 12: v = 32; break;                   // icons
  case 13: case 14: v = 32; break;                   // cursor
  case 19: v = 1; break;                             // SM_MOUSEPRESENT
  case 32: case 33: v = 4; break;                    // sizing frames
  case 43: v = 3; break;                             // SM_CMOUSEBUTTONS
  case 75: v = 1; break;                             // SM_MOUSEWHEELPRESENT
  case 80: v = 1; break;                             // SM_CMONITORS
  }
  w32_ret(c, v, 1);
}
static void u_EnumDisplaySettingsA(rc_ctx *c) {  // DEVMODEA: the current mode for ENUM_CURRENT_SETTINGS, else one mode
  uint32_t n = w32_arg(c, 1), dm = w32_arg(c, 2);
  if (n != 0xffffffffu && n != 0xfffffffeu && n > 0) { w32_ret(c, 0, 3); return; }
  int32_t w, h;
  display_size(&w, &h);
  uint16_t sz = 156;
  memset(RC32_H(dm), 0, sz);
  snprintf((char *)RC32_H(dm), 32, "Display");
  *(uint16_t *)RC32_H(dm + 36) = sz;  // dmSize
  w32_wr32(dm + 40, 0x5c0000);        // dmFields: BITSPERPEL | PELSWIDTH | PELSHEIGHT | DISPLAYFREQUENCY
  w32_wr32(dm + 104, 32);             // dmBitsPerPel
  w32_wr32(dm + 108, (uint32_t)w);
  w32_wr32(dm + 112, (uint32_t)h);
  w32_wr32(dm + 120, 60);             // dmDisplayFrequency
  w32_ret(c, 1, 3);
}
// the real display mode never changes: the game's modes are windows (win-x86/app/lp_window.m)
static void u_ChangeDisplaySettingsA(rc_ctx *c) {
  if (w32_log) fprintf(stderr, "w32: ChangeDisplaySettings(0x%x, 0x%x)\n", w32_arg(c, 0), w32_arg(c, 1));
  w32_ret(c, 0, 2);
}  // DISP_CHANGE_SUCCESSFUL

// --- messages: one queue for the process -----------------------------------------------------
#define QN 256
static struct { uint32_t h, msg, wp, lp; } q[QN];
static int qh, qt;
static int quit;
static uint32_t quit_code;
static void post(uint32_t h, uint32_t msg, uint32_t wp, uint32_t lp) {
  pthread_mutex_lock(&mu);
  if ((qt + 1) % QN != qh) {
    q[qt].h = h; q[qt].msg = msg; q[qt].wp = wp; q[qt].lp = lp;
    qt = (qt + 1) % QN;
  }
  pthread_mutex_unlock(&mu);
}
// input from the host window (win32_input.c): to the first top-level window, the game's
void w32_post_input(uint32_t msg, uint32_t wp, uint32_t lp) {
  uint32_t h = 0;
  pthread_mutex_lock(&mu);
  for (int k = 0; k < MAXWND - 1 && !h; k++)
    if (wnds[k].used && !wnds[k].parent) h = HWND_BASE + 4 * (uint32_t)k;
  pthread_mutex_unlock(&mu);
  if (h) post(h, msg, wp, lp);
}
// The window gained or lost the keyboard: what Windows sends the application's window then. Posted:
// the game's thread takes them from its message loop.
void w32_user_focus(int active) {
  w32_post_input(0x1c, (uint32_t)active, 0);  // WM_ACTIVATEAPP
  w32_post_input(0x06, (uint32_t)active, 0);  // WM_ACTIVATE: WA_ACTIVE or WA_INACTIVE
}
static void u_PostMessageA(rc_ctx *c) { post(w32_arg(c, 0), w32_arg(c, 1), w32_arg(c, 2), w32_arg(c, 3)); w32_ret(c, 1, 4); }
static void u_PostMessageW(rc_ctx *c) { u_PostMessageA(c); }
static void u_PostQuitMessage(rc_ctx *c) { quit = 1; quit_code = w32_arg(c, 0); w32_ret(c, 0, 1); }
// MSG: hwnd, message, wParam, lParam, time, pt
static int peek(uint32_t m, uint32_t fl) {
  pthread_mutex_lock(&mu);
  int got = 0;
  if (qh != qt) {
    uint32_t v[7] = {q[qh].h, q[qh].msg, q[qh].wp, q[qh].lp, (uint32_t)(clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000), 0, 0};
    memcpy(RC32_H(m), v, 28);
    if (fl & 1) qh = (qh + 1) % QN;  // PM_REMOVE
    got = 1;
  } else if (quit) {
    uint32_t v[7] = {0, 0x12, quit_code, 0, 0, 0, 0};  // WM_QUIT
    memcpy(RC32_H(m), v, 28);
    if (fl & 1) quit = 0;
    got = 1;
  }
  pthread_mutex_unlock(&mu);
  return got;
}
// The top of the game's message loop, on the thread that peeks (win32_overrides.c: a resolution the
// host window asked for is applied here, as the game's own F11 is from its window procedure).
__attribute__((weak)) void w32_pump(rc_ctx *c) { (void)c; }
static void u_PeekMessageA(rc_ctx *c) {
  w32_pump(c);
  w32_ret(c, (uint32_t)peek(w32_arg(c, 0), w32_arg(c, 4)), 5);
}
// The host resized a window's client area, as a user's drag does on Windows: GetClientRect says so.
void w32_window_size(uint32_t h, int w, int ht) {
  pthread_mutex_lock(&mu);
  wnd *o = wnd_of(h);
  if (o && w > 0 && ht > 0) {
    o->w = w;
    o->h = ht;
  }
  pthread_mutex_unlock(&mu);
}
static void u_PeekMessageW(rc_ctx *c) { u_PeekMessageA(c); }
static void u_GetMessageA(rc_ctx *c) {
  uint32_t m = w32_arg(c, 0);
  while (!peek(m, 1)) usleep(1000);
  w32_ret(c, w32_rd32(m + 4) != 0x12, 4);
}
static void u_GetMessageW(rc_ctx *c) { u_GetMessageA(c); }
static void dispatch(rc_ctx *c) {
  uint32_t m = w32_arg(c, 0);
  wnd *o = wnd_of(w32_rd32(m));
  w32_ret(c, o ? call_proc(c, o->proc, w32_rd32(m), w32_rd32(m + 4), w32_rd32(m + 8), w32_rd32(m + 12)) : 0, 1);
}
static void u_DispatchMessageA(rc_ctx *c) { dispatch(c); }
static void u_DispatchMessageW(rc_ctx *c) { dispatch(c); }
static void u_TranslateMessage(rc_ctx *c) { w32_ret(c, 0, 1); }
static void u_TranslateAccelerator(rc_ctx *c) { w32_ret(c, 0, 3); }
static void u_TranslateAcceleratorA(rc_ctx *c) { w32_ret(c, 0, 3); }
static void u_TranslateAcceleratorW(rc_ctx *c) { w32_ret(c, 0, 3); }

// --- cursor, keyboard ------------------------------------------------------------------------
static int cursor_count;
static void u_ShowCursor(rc_ctx *c) { cursor_count += w32_arg(c, 0) ? 1 : -1; w32_ret(c, (uint32_t)cursor_count, 1); }
static void u_SetCursor(rc_ctx *c) { w32_ret(c, 0, 1); }
static void u_LoadCursorA(rc_ctx *c) { w32_ret(c, 0x00c50000u | (w32_arg(c, 1) & 0xffff), 2); }
static void u_LoadCursorW(rc_ctx *c) { w32_ret(c, 0x00c50000u | (w32_arg(c, 1) & 0xffff), 2); }
static void u_LoadIconA(rc_ctx *c) { w32_ret(c, 0x00c60000u | (w32_arg(c, 1) & 0xffff), 2); }
static void u_LoadIconW(rc_ctx *c) { w32_ret(c, 0x00c60000u | (w32_arg(c, 1) & 0xffff), 2); }
static void u_GetCursorInfo(rc_ctx *c) {  // CURSORINFO: cbSize, flags, hCursor, pt
  uint32_t p = w32_arg(c, 0);
  w32_wr32(p + 4, cursor_count >= 0 ? 1 : 0);  // CURSOR_SHOWING
  w32_wr32(p + 8, 0x00c57f00u);
  w32_wr64(p + 12, 0);
  w32_ret(c, 1, 1);
}
static void u_GetCursorPos(rc_ctx *c) {  // the window is at 0, 0: client and screen coincide
  int32_t x, y;
  w32_input_cursor(&x, &y);
  w32_wr32(w32_arg(c, 0), (uint32_t)x);
  w32_wr32(w32_arg(c, 0) + 4, (uint32_t)y);
  w32_ret(c, 1, 1);
}
static void u_SetCursorPos(rc_ctx *c) { w32_ret(c, 1, 2); }
static void u_ClipCursor(rc_ctx *c) { w32_ret(c, 1, 1); }
static void u_GetKeyboardState(rc_ctx *c) { w32_input_keyboard_vk(w32_arg(c, 0)); w32_ret(c, 1, 1); }
static void u_GetKeyState(rc_ctx *c) { w32_ret(c, w32_input_vk(w32_arg(c, 0), 0), 1); }
static void u_GetAsyncKeyState(rc_ctx *c) { w32_ret(c, w32_input_vk(w32_arg(c, 0), 1), 1); }
static void u_MapVirtualKeyA(rc_ctx *c) { w32_ret(c, 0, 2); }
// ToAscii / ToUnicode (vk, scan, state, out, [cap,] flags): letters, digits and space by virtual key
static int vk_char(uint32_t vk, const uint8_t *st) {
  int shift = (st[0x10] & 0x80) != 0;
  if (vk >= 'A' && vk <= 'Z') return shift ? (int)vk : (int)vk + 32;
  if (vk >= '0' && vk <= '9') return shift ? ")!@#$%^&*("[vk - '0'] : (int)vk;
  if (vk == 0x20) return ' ';
  if (vk == 0x0d) return '\r';
  if (vk == 0x08) return 8;
  return 0;
}
static void u_ToAscii(rc_ctx *c) {
  int ch = vk_char(w32_arg(c, 0), (const uint8_t *)RC32_H(w32_arg(c, 2)));
  if (ch) *(uint16_t *)RC32_H(w32_arg(c, 3)) = (uint16_t)ch;
  w32_ret(c, ch ? 1 : 0, 5);
}
static void u_ToUnicode(rc_ctx *c) {
  int ch = vk_char(w32_arg(c, 0), (const uint8_t *)RC32_H(w32_arg(c, 2)));
  if (ch && w32_arg(c, 4)) *(uint16_t *)RC32_H(w32_arg(c, 3)) = (uint16_t)ch;
  w32_ret(c, ch ? 1 : 0, 6);
}

// --- clipboard, dialogs, message boxes, misc ---------------------------------------------------
static void u_OpenClipboard(rc_ctx *c) { w32_ret(c, 1, 1); }
static void u_CloseClipboard(rc_ctx *c) { w32_ret(c, 1, 0); }
static void u_GetClipboardData(rc_ctx *c) { w32_ret(c, 0, 1); }
static void u_EmptyClipboard(rc_ctx *c) { w32_ret(c, 1, 0); }
static void u_SetClipboardData(rc_ctx *c) { w32_ret(c, w32_arg(c, 1), 2); }
static void u_CreateDialogParamA(rc_ctx *c) {
  fprintf(stderr, "w32: CreateDialogParam(template %u): no dialogs\n", w32_arg(c, 1));
  w32_ret(c, 0, 5);
}
static void u_GetDlgItem(rc_ctx *c) { w32_ret(c, 0, 2); }
static void u_SendDlgItemMessageA(rc_ctx *c) { w32_ret(c, 0, 5); }
static void u_MessageBoxA(rc_ctx *c) {
  fprintf(stderr, "w32: MessageBox \"%s\": %s\n", w32_cstr(w32_arg(c, 2)) ? w32_cstr(w32_arg(c, 2)) : "",
          w32_cstr(w32_arg(c, 1)) ? w32_cstr(w32_arg(c, 1)) : "");
  uint32_t t = w32_arg(c, 3) & 0xf;
  w32_ret(c, t == 4 || t == 3 ? 6 : 1, 4);  // IDYES for yes/no boxes, else IDOK
}
static void u_MessageBoxW(rc_ctx *c) {
  char a[512], b[2048];
  fprintf(stderr, "w32: MessageBox \"%s\": %s\n", w32_wstr(w32_arg(c, 2), a, sizeof a), w32_wstr(w32_arg(c, 1), b, sizeof b));
  uint32_t t = w32_arg(c, 3) & 0xf;
  w32_ret(c, t == 4 || t == 3 ? 6 : 1, 4);
}
static void u_RegisterDeviceNotificationW(rc_ctx *c) { w32_ret(c, 0x00de0001u, 3); }
static void u_RegisterDeviceNotificationA(rc_ctx *c) { w32_ret(c, 0x00de0001u, 3); }
static void u_UnregisterDeviceNotification(rc_ctx *c) { w32_ret(c, 1, 1); }
static void u_GetProcessWindowStation(rc_ctx *c) { w32_ret(c, 0x00e50001u, 0); }
static void u_GetUserObjectInformationW(rc_ctx *c) {  // UOI_FLAGS: WSF_VISIBLE (an interactive window station)
  uint32_t i = w32_arg(c, 1), b = w32_arg(c, 2), n = w32_arg(c, 3), need = w32_arg(c, 4);
  if (i == 1 && n >= 12) {
    w32_wr32(b, 0);
    w32_wr32(b + 4, 0);
    w32_wr32(b + 8, 1);
  }
  if (need) w32_wr32(need, 12);
  w32_ret(c, i == 1 && n >= 12, 5);
}
static void u_wsprintfA(rc_ctx *c) {  // cdecl; only "%s"/"%d"/"%u"/"%x" without flags
  uint32_t out = w32_arg(c, 0);
  const char *f = w32_cstr(w32_arg(c, 1));
  char s[1024];
  size_t o = 0;
  int ai = 2;
  for (; *f && o < sizeof s - 32; f++) {
    if (*f != '%' || !f[1]) { s[o++] = *f; continue; }
    f++;
    if (*f == 's') o += (size_t)snprintf(s + o, sizeof s - o, "%s", w32_cstr(w32_arg(c, ai++)));
    else if (*f == 'd' || *f == 'i') o += (size_t)snprintf(s + o, sizeof s - o, "%d", (int32_t)w32_arg(c, ai++));
    else if (*f == 'u') o += (size_t)snprintf(s + o, sizeof s - o, "%u", w32_arg(c, ai++));
    else if (*f == 'x') o += (size_t)snprintf(s + o, sizeof s - o, "%x", w32_arg(c, ai++));
    else s[o++] = *f;
    if (o >= sizeof s) o = sizeof s - 1;  // snprintf counts what did not fit
  }
  s[o] = 0;
  memcpy(RC32_H(out), s, o + 1);
  c->r[0] = (uint32_t)o;
}

#define E(n) {#n, u_##n}
static const w32_export fns[] = {
    E(RegisterClassA), E(RegisterClassW), E(RegisterClassExA), E(RegisterClassExW), E(UnregisterClassA),
    E(UnregisterClassW), E(CreateWindowExA), E(CreateWindowExW), E(DestroyWindow), E(DefWindowProcA),
    E(DefWindowProcW), E(CallWindowProcA), E(CallWindowProcW), E(SendMessageA), E(SendMessageW),
    E(GetWindowLongA), E(GetWindowLongW), E(SetWindowLongA), E(SetWindowLongW), E(ShowWindow), E(UpdateWindow),
    E(EnableWindow), E(SetWindowPos), E(MoveWindow), E(GetClientRect), E(GetWindowRect), E(GetWindowInfo),
    E(AdjustWindowRect), E(AdjustWindowRectEx), E(ClientToScreen), E(ScreenToClient), E(GetFocus), E(SetFocus),
    E(GetForegroundWindow), E(SetForegroundWindow), E(GetActiveWindow), E(GetDesktopWindow), E(IsWindow),
    E(IsWindowVisible), E(IsIconic), E(GetDC), E(ReleaseDC), E(SetWindowTextA), E(SetWindowTextW),
    E(GetSystemMetrics), E(EnumDisplaySettingsA), E(ChangeDisplaySettingsA), E(PostMessageA), E(PostMessageW),
    E(PostQuitMessage), E(PeekMessageA), E(PeekMessageW), E(GetMessageA), E(GetMessageW), E(DispatchMessageA),
    E(DispatchMessageW), E(TranslateMessage), E(TranslateAccelerator), E(TranslateAcceleratorA),
    E(TranslateAcceleratorW), E(ShowCursor), E(SetCursor), E(LoadCursorA), E(LoadCursorW), E(LoadIconA),
    E(LoadIconW), E(GetCursorInfo), E(GetCursorPos), E(SetCursorPos), E(ClipCursor), E(GetKeyboardState),
    E(GetKeyState), E(GetAsyncKeyState), E(MapVirtualKeyA), E(ToAscii), E(ToUnicode), E(OpenClipboard),
    E(CloseClipboard), E(GetClipboardData), E(EmptyClipboard), E(SetClipboardData), E(CreateDialogParamA),
    E(GetDlgItem), E(SendDlgItemMessageA), E(MessageBoxA), E(MessageBoxW), E(RegisterDeviceNotificationW),
    E(RegisterDeviceNotificationA), E(UnregisterDeviceNotification), E(GetProcessWindowStation),
    E(GetUserObjectInformationW), E(wsprintfA),
};
W32_DLL(w32_dll_user32, "user32.dll", fns, 0);
