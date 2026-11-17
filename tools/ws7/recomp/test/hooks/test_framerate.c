// The frame rate fixes' arithmetic (overrides/mods/framerate.h), without a game: at 60 Hz and below nothing
// changes, and above it the windows, steps, gates and counts come out as a 60 Hz game has them.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mods/framerate.h"

static int g_fail;
#define CHECK(c, ...) \
  do { \
    if (!(c)) { \
      g_fail++; \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__); \
      printf("\n"); \
    } \
  } while (0)

// The game's frame time: whole milliseconds between frames that start every `period` seconds
static float ms_dt(double period, int frame) {
  long a = (long)floor(period * 1000.0 * frame), b = (long)floor(period * 1000.0 * (frame + 1));
  return (float)(b - a) / 1000.0f;
}

// Calls passed by the gate in `seconds` at a frame every `period`
static int gate_passes(double period, double seconds) {
  rs_fr_slot s = {0};
  int n = 0;
  for (int i = 0; i * period < seconds; i++) n += rs_fr_gate(&s, ms_dt(period, i));
  return n;
}

// The time from a run's first miss to the call that counts three, at a frame every `period`
static double miss_time(double period, int start) {
  rs_fr_slot s = {0};
  double t = 0;
  for (uint32_t misses = 1; misses < 1000; misses++) {
    float dt = ms_dt(period, start + (int)misses);
    if (misses > 1) t += dt;
    if (rs_fr_misses(&s, misses, misses > 1 ? dt : 0.016f) >= 3) return t;
  }
  return -1;
}

int main(void) {
  // which frames are faster than 60 Hz: 16 and 17 ms are not, 13 ms (75 Hz) and less are
  CHECK(!rs_fr_fast(0.016f) && !rs_fr_fast(0.017f) && !rs_fr_fast(0.0666667f), "60 Hz and slower");
  CHECK(!rs_fr_fast(0.015f), "a 60 Hz frame that came a millisecond early");
  CHECK(rs_fr_fast(0.014f) && rs_fr_fast(0.008f) && rs_fr_fast(0.0f), "faster");

  // the window: the game's own value where it fits the buffer, and never past it
  CHECK(rs_fr_window(58) == 58 && rs_fr_window(62) == 62 && rs_fr_window(30) == 30, "60 Hz: as the game has it");
  CHECK(rs_fr_window(100) == 100 && rs_fr_window(111) == 100 && rs_fr_window(125) == 100, "120 Hz: the buffer");
  CHECK(rs_fr_window(250) == 100 && rs_fr_window(1000) == 100 && rs_fr_window(0xffffffffu) == 100, "a short frame");
  CHECK(rs_fr_window(0) == 30, "a frame of 0 ms: the game's 0 becomes its lower bound");

  // a step: untouched at 60 Hz and below, in proportion above, nothing for a frame of no time
  CHECK(rs_fr_step(0.001f, 0.016f) == 0.001f && rs_fr_step(-0.04f, 0.017f) == -0.04f, "60 Hz: the game's step");
  CHECK(rs_fr_step(0.001f, 0.0666667f) == 0.001f, "a slow frame: the game's step");
  CHECK(fabsf(rs_fr_step(0.001f, 0.008f) - 0.00048f) < 1e-7f, "8 ms: %g", rs_fr_step(0.001f, 0.008f));
  CHECK(fabsf(rs_fr_step(-0.04f, 0.004f) + 0.0096f) < 1e-6f, "4 ms: %g", rs_fr_step(-0.04f, 0.004f));
  CHECK(rs_fr_step(0.001f, 0.0f) == 0.0f, "0 ms");
  // the panel's hold: max(dt, 1/15) a frame is four seconds a second at 60 Hz, and stays so
  CHECK(fabsf(rs_fr_step(0.0666667f, 0.008f) / 0.008f - 4.0f) < 1e-3f, "the hold's rate at 8 ms");
  // over a second the steps add up to the 60 of a 60 Hz game, at any faster rate
  for (int hz = 75; hz <= 360; hz += 15) {
    double sum = 0;
    for (int i = 0; i < hz; i++) sum += rs_fr_step(1.0f, ms_dt(1.0 / hz, i));
    CHECK(fabs(sum - 60.0) < 0.6, "%d Hz: %g steps a second", hz, sum);
  }

  // the gate: every call at 60 Hz and below, 60 a second above
  CHECK(gate_passes(1.0 / 60, 10) == 600, "60 Hz: %d", gate_passes(1.0 / 60, 10));
  CHECK(gate_passes(1.0 / 30, 10) == 300, "30 Hz: %d", gate_passes(1.0 / 30, 10));
  for (int hz = 75; hz <= 360; hz += 15) {
    int n = gate_passes(1.0 / hz, 10);
    CHECK(n >= 594 && n <= 606, "%d Hz: %d calls in 10 s", hz, n);
  }
  {  // a 60 Hz game that is late with a frame makes the next one short: both calls are the game's own
    rs_fr_slot s = {0};
    int n = 0, frames = 0;
    for (int i = 0; i < 600; i++) {
      int k = i % 10;
      float dt = k == 3 ? 0.022f : k == 4 ? 0.011f : (i & 1 ? 0.017f : 0.016f);
      n += rs_fr_gate(&s, dt), frames++;
    }
    CHECK(n == frames, "60 Hz with late frames: %d of %d calls", n, frames);
  }
  for (int fps = 68; fps <= 74; fps++) {  // where 14 and 15 ms frames mix (up to 66 every frame reads 15 ms or more)
    int n = gate_passes(1.0 / fps, 10);
    CHECK(n >= 598 && n <= 615, "%d fps: %d calls in 10 s", fps, n);
  }
  {  // at 120 Hz it is every other call, not a burst and a gap
    rs_fr_slot s = {0};
    int run = 0, worst = 0;
    for (int i = 0; i < 1200; i++) {
      if (rs_fr_gate(&s, ms_dt(1.0 / 120, i))) run = 0;
      else if (++run > worst) worst = run;
    }
    CHECK(worst <= 2, "120 Hz: %d calls skipped in a row", worst);
  }
  {  // a frame of no time passes nothing and breaks nothing
    rs_fr_slot s = {0};
    for (int i = 0; i < 100; i++) CHECK(!rs_fr_gate(&s, 0.0f), "0 ms");
    CHECK(s.t == 0, "0 ms: %g kept", s.t);
  }

  // the misses: the game's own count at 60 Hz and below
  {
    rs_fr_slot s = {0};
    for (uint32_t m = 1; m <= 10; m++) CHECK(rs_fr_misses(&s, m, m & 1 ? 0.016f : 0.017f) == m, "60 Hz: %u", m);
  }
  // three are counted two 60 Hz frames after the first, at any faster rate and from any phase of the milliseconds
  for (int hz = 75; hz <= 360; hz += 15)
    for (int start = 0; start < 7; start++) {
      double t = miss_time(1.0 / hz, start);
      CHECK(t >= 0.032 && t <= 0.034 + 1.0 / hz, "%d Hz, from frame %d: three after %.4f s", hz, start, t);
    }
  {  // never more than the game counted, and a new run starts again
    rs_fr_slot s = {0};
    CHECK(rs_fr_misses(&s, 1, 0.008f) == 1 && rs_fr_misses(&s, 2, 0.008f) == 1, "8 ms: the second call is still one");
    CHECK(rs_fr_misses(&s, 3, 0.008f) == 2 && rs_fr_misses(&s, 4, 0.009f) == 2, "16 and 25 ms: two");
    CHECK(rs_fr_misses(&s, 5, 0.008f) == 3, "33 ms: three");
    CHECK(rs_fr_misses(&s, 1, 0.008f) == 1 && s.t == 0, "a new run");
    for (uint32_t m = 2; m < 50; m++) CHECK(rs_fr_misses(&s, m, 0.004f) <= m, "never more than the game's");
  }

  // the slots: one per object, the oldest given up for a fifth
  {
    rs_fr_slots t;
    memset(&t, 0, sizeof t);
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    rs_fr_slot *sa = rs_fr_slot_of(&t, &a), *sb = rs_fr_slot_of(&t, &b);
    CHECK(sa != sb && rs_fr_slot_of(&t, &a) == sa && rs_fr_slot_of(&t, &b) == sb, "two objects, two slots");
    sa->t = 0.5f;
    rs_fr_slot_of(&t, &c);
    rs_fr_slot_of(&t, &d);
    CHECK(rs_fr_slot_of(&t, &a) == sa && sa->t == 0.5f, "four objects keep theirs");
    rs_fr_slot *se = rs_fr_slot_of(&t, &e);
    CHECK(se->who == &e && se->t == 0, "a fifth starts clean");
  }

  if (g_fail) printf("test_framerate: %d failure(s)\n", g_fail);
  else printf("test_framerate: ok\n");
  return g_fail != 0;
}
