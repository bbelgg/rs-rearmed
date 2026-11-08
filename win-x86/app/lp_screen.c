// The game window's rules (lp_screen.h): mode and size decisions of lp_window.m without AppKit.
#include "lp_screen.h"

#include <math.h>

#define WS_POPUP 0x80000000u
#define WS_CAPTION 0x00C00000u

int lp_force_mode(const char *env) {
  if (!env || !*env) return -1;
  return *env != '0';
}

int lp_style_full(uint32_t style) { return (style & WS_POPUP) && (style & WS_CAPTION) != WS_CAPTION; }

int lp_initial_full(int force, int windowed, int w, int h, int dw, int dh) {
  if (force >= 0) return force;
  return !windowed || (w == dw && h == dh);
}

void lp_fit_window(int w, int h, double scale, double vw, double vh, double *ow, double *oh) {
  if (scale <= 0) scale = 1;
  double ww = w > 0 ? w / scale : 1, wh = h > 0 ? h / scale : 1;
  double k = 1;
  if (vw > 0 && vh > 0) k = fmin(1, fmin(vw / ww, vh / wh));
  *ow = fmax(1, floor(ww * k));
  *oh = fmax(1, floor(wh * k));
}

void lp_view_to_pixels(double px, double py, double vw, double vh, int w, int h, int32_t *x, int32_t *y,
                       double *k) {
  double s = w > 0 && h > 0 && vw > 0 && vh > 0 ? fmin(vw / w, vh / h) : 1;
  double ox = (vw - w * s) / 2, oy = (vh - h * s) / 2;
  double fx = floor((px - ox) / s), fy = floor((py - oy) / s);
  if (fx < 0) fx = 0;
  if (fy < 0) fy = 0;
  if (fx > w - 1) fx = w - 1;
  if (fy > h - 1) fy = h - 1;
  *x = (int32_t)fx;
  *y = (int32_t)fy;
  *k = s;
}

int lp_any_shape(const char *env) {
  return !(env && *env && (*env == '0' || *env == 'n' || *env == 'N' || *env == 'f' || *env == 'F'));
}

int lp_follow_size(double cw, double ch, double scale, int bw, int bh, int *w, int *h) {
  if (scale <= 0) scale = 1;
  double fw = floor(cw * scale + 1e-6), fh = floor(ch * scale + 1e-6);
  *w = fw < 1 ? 1 : (int)fw;
  *h = fh < 1 ? 1 : (int)fh;
  return *w != bw || *h != bh;
}
