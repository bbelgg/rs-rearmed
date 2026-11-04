// Host test of the game window's rules (win-x86/app/lp_screen.c): the mode a style or a back
// buffer means, the RS_FULLSCREEN override, the window size on a small screen, and the pointer
// mapping into a letterboxed back buffer. Prints "N passed, 0 failed".
//
//   cc -I win-x86/app win-x86/test/lp_screen_test.c win-x86/app/lp_screen.c -o lp_screen_test
#include <stdio.h>

#include "lp_screen.h"

static int passed, failed;
#define CHECK(c, ...)                         \
  do {                                        \
    if (c) passed++;                          \
    else {                                    \
      failed++;                               \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                    \
      printf("\n");                           \
    }                                         \
  } while (0)

int main(void) {
  // RS_FULLSCREEN
  CHECK(lp_force_mode(NULL) == -1 && lp_force_mode("") == -1, "unset: the game decides");
  CHECK(lp_force_mode("1") == 1 && lp_force_mode("yes") == 1, "1: full screen");
  CHECK(lp_force_mode("0") == 0, "0: windowed");

  // the styles the game sets (seen in its runs): Fullscreen=1 and Fullscreen=0
  CHECK(lp_style_full(0x90000000u), "WS_POPUP | WS_VISIBLE is full screen");
  CHECK(!lp_style_full(0x10ca0000u), "caption, sysmenu, minimize box is a window");
  CHECK(!lp_style_full(0x00ca0000u), "the creation style is a window");
  CHECK(!lp_style_full(0x80c00000u), "a popup with a caption is a window");

  // a new device: the desktop-sized back buffer of Fullscreen=1, the 1280x720 of Fullscreen=0
  CHECK(lp_initial_full(-1, 1, 1470, 956, 1470, 956), "desktop-sized back buffer: full screen");
  CHECK(!lp_initial_full(-1, 1, 1280, 720, 1470, 956), "1280x720 on 1470x956: windowed");
  CHECK(lp_initial_full(-1, 0, 1280, 720, 1470, 956), "a full-screen device (Fullscreen=2): full screen");
  CHECK(!lp_initial_full(0, 1, 1470, 956, 1470, 956), "RS_FULLSCREEN=0 wins");
  CHECK(!lp_initial_full(0, 0, 1280, 720, 1470, 956), "RS_FULLSCREEN=0 wins over a full-screen device");
  CHECK(lp_initial_full(1, 1, 1280, 720, 1470, 956), "RS_FULLSCREEN=1 wins");

  // window sizes: 1280x720 fits a 1470x956-point screen as it is (content area 1470x894)
  double w, h;
  lp_fit_window(1280, 720, 1, 1470, 894, &w, &h);
  CHECK(w == 1280 && h == 720, "1280x720 at 1x: %gx%g", w, h);
  // the old bug: a 1920x1080 window on that screen; now shrunk, aspect kept
  lp_fit_window(1920, 1080, 1, 1470, 894, &w, &h);
  CHECK(w == 1470 && h == 826, "1920x1080 shrunk to 1470x826: %gx%g", w, h);
  // a desktop-sized back buffer windowed (RS_FULLSCREEN=0 with Fullscreen=1): height-bound
  lp_fit_window(1470, 956, 1, 1470, 894, &w, &h);
  CHECK(w == 1374 && h == 894, "1470x956 into 1470x894: %gx%g", w, h);
  // RS_METAL_RETINA=1: pixels at scale 2 are half as many points
  lp_fit_window(2560, 1440, 2, 1470, 894, &w, &h);
  CHECK(w == 1280 && h == 720, "2560x1440 at 2x: %gx%g", w, h);
  lp_fit_window(0, 0, 0, 0, 0, &w, &h);
  CHECK(w >= 1 && h >= 1, "degenerate sizes stay positive");

  // the pointer: a 1280x720 back buffer in a 1000x400 view is 711x400 points, 144.5 points in
  int32_t x, y;
  double k;
  lp_view_to_pixels(500, 200, 1000, 400, 1280, 720, &x, &y, &k);
  CHECK(x == 640 && y == 360, "centre -> centre: %d,%d", x, y);
  CHECK(k > 0.555 && k < 0.556, "scale 400/720: %g", k);
  lp_view_to_pixels(10, 10, 1000, 400, 1280, 720, &x, &y, &k);
  CHECK(x == 0 && y == 18, "left bar clamps to column 0: %d,%d", x, y);
  lp_view_to_pixels(999, 399.9, 1000, 400, 1280, 720, &x, &y, &k);
  CHECK(x == 1279 && y == 719, "bottom right clamps: %d,%d", x, y);
  // same shape: one to one at 1x
  lp_view_to_pixels(100.5, 50.5, 1280, 720, 1280, 720, &x, &y, &k);
  CHECK(x == 100 && y == 50 && k == 1, "1:1: %d,%d %g", x, y, k);
  // a full screen of another shape (1280x720 on 1470x956): bars top and bottom
  lp_view_to_pixels(735, 64, 1470, 956, 1280, 720, &x, &y, &k);
  CHECK(x == 640 && y == 0, "top bar clamps to row 0: %d,%d", x, y);

  printf("%d passed, %d failed\n", passed, failed);
  return failed != 0;
}
