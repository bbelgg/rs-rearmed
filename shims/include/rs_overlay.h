// RS Re-ARMed overlay API: 2D drawing over the game's frame for mods (MIT).
//
// Exported by the game executable (Rocksmith2014) from RS Re-ARMed 1.3.0 on. A mod registers a callback;
// the shell calls it on the render thread once per frame, after the game has drawn the frame and before it
// is shown. Inside the callback, and only there, the mod draws with the rs_overlay_* calls below. The calls
// are backend-neutral: the mod never sees the graphics API (Metal or OpenGL) and must not call it
// itself. The implementation saves and restores the game's state around the callbacks.
//
// Rules (as X-Plane's for its plugins): no GL calls and no glGet of your own, no access to the game's
// textures or buffers, images only through rs_overlay_image_*.
//
// Coordinates are pixels of the window's framebuffer, origin top left, x right, y down. Images are RGBA,
// 8 bits per channel, R first in memory, rows top first, alpha premultiplied (as a CGBitmapContext with
// kCGImageAlphaPremultipliedLast draws them). Colours are 0xRRGGBBAA, not premultiplied.
//
// Detecting the API from a mod that also runs on older executables:
//
//   int (*ver)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_overlay_api_version");
//   if (ver && ver() >= 1) { ...dlsym the functions below... } else { ...old path... }
//
// Version 1: everything in this header.
#ifndef RS_OVERLAY_H
#define RS_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS_OVERLAY_API_VERSION 1

typedef struct rs_overlay_frame {
  int width, height;  // framebuffer size in pixels
  uint32_t index;     // frame counter (increments once per shown frame with callbacks)
} rs_overlay_frame;

typedef struct rs_overlay_image rs_overlay_image;

// The callback; frame is valid until it returns.
typedef void (*rs_overlay_fn)(rs_overlay_frame *frame, void *user);

// The API version this executable implements (RS_OVERLAY_API_VERSION of its build).
int rs_overlay_api_version(void);

// Adds a callback (any thread). Callbacks run in registration order. Returns 0, or -1 if the table
// (16 entries) is full or fn is NULL. The same (fn, user) pair registered twice runs twice.
int rs_overlay_register(rs_overlay_fn fn, void *user);
// Removes the first registration of (fn, user) (any thread, also from inside a callback). 0, or -1.
// When it returns, that callback is not running and does not run again, so user may be freed (called
// from another thread it waits for a running call to return: do not hold a lock the callback takes;
// called from inside the callback itself, that call finishes normally).
int rs_overlay_unregister(rs_overlay_fn fn, void *user);

// --- inside a callback only, on the thread that runs it (they return NULL / -1 or do nothing elsewhere) ---

// A w x h image, initially transparent. It lives until rs_overlay_image_destroy, across frames.
rs_overlay_image *rs_overlay_image_create(rs_overlay_frame *frame, int w, int h);
// Replaces the whole image: rgba is w x h pixels as described above, stride bytes per row (0: w * 4).
int rs_overlay_image_update(rs_overlay_frame *frame, rs_overlay_image *img, const void *rgba, int stride);
void rs_overlay_image_destroy(rs_overlay_frame *frame, rs_overlay_image *img);
// Image size; works outside callbacks too.
void rs_overlay_image_size(const rs_overlay_image *img, int *w, int *h);

// Draws img stretched to the rect (x, y, w, h), blended over the frame (premultiplied "over"), with its
// colour and alpha scaled by alpha (0..1). Linear filtering; at its own size on whole pixels it is exact.
int rs_overlay_draw_image(rs_overlay_frame *frame, rs_overlay_image *img, float x, float y, float w, float h,
                          float alpha);
// Fills the rect with rgba (0xRRGGBBAA), blended over the frame.
int rs_overlay_fill_rect(rs_overlay_frame *frame, float x, float y, float w, float h, uint32_t rgba);

// Copies the frame's pixels in (x, y, w, h), as drawn so far (the game's frame and the overlays drawn before
// this call), into out: w * h RGBA pixels, rows top first, w * 4 bytes per row, alpha 255.
// Slow (waits for the GPU); for screenshots. 0, or -1 if the rect is not inside the
// frame.
int rs_overlay_read_pixels(rs_overlay_frame *frame, int x, int y, int w, int h, void *out);

#ifdef __cplusplus
}
#endif

#endif
