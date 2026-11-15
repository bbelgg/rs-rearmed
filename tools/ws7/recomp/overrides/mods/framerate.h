// Frame rate: the game's calibration and tuner at any frame rate as they are at 60 Hz.
//
// The game counts some things in frames. Its frame time is whole milliseconds, capped at 1/15 s and with no lower
// bound (the frame tick, 0x1000e0330 on the Mac), and above 60 frames a second these go wrong:
//
//   * Calibration (UIMenu_CalibrationMeter) sizes its averaging window when the screen opens, as the frames of one
//     second by that one frame's time (max(30, 1/dt), no upper bound, 0 for a frame of 0 ms). The samples go to 100
//     floats a player, two players back to back, with the meter's own counters and timers right behind: above 100
//     frames a second player 1's samples run into player 2's, above 200 into the meter's own state.
//   * Calibration moves the input level one step a frame toward its target, in both of its phases (the strings
//     muted, then played), while its average lags half a second: twice the frames, twice the speed.
//   * The tuner gives a string that drifts out of tune two frames before its hold loses ground, and averages the
//     last 20 levels (the volume warnings) and the last 10 notes (the wrong instrument warning), one a frame.
//   * A hold of a menu panel takes at least 1/15 s off its timer every frame, whatever the frame's time.
//
// Each is put right at a mid point (rc_mid.h; framerate.c has the hooks), by the frame's own time, and only for a
// frame shorter than a 60 Hz one: at 60 Hz and below the game runs as it always has. RS_MODS_FRAME_RATE=0 leaves
// every rate to the game.
//
// What is here is the arithmetic, apart from the game, for the hooks and their test.
#ifndef RS_FRAMERATE_H
#define RS_FRAMERATE_H

#include <stdint.h>

#define RS_FR_REF_DT (1.0f / 60.0f)  // the frame the game's counts were made for
#define RS_FR_FAST_DT 0.0145f        // a shorter frame is a faster game than 60 Hz, whose frames read 16 or 17 ms, 15 at times
#define RS_FR_TOL 0.001f             // a frame's time is whole milliseconds: two of 8 ms are one 60 Hz frame
#define RS_FR_WINDOW_MIN 30          // the game's own lower bound of the calibration window
#define RS_FR_WINDOW_MAX 100         // the samples a player's buffer holds
#define RS_FR_MISSES 3               // the misses in a row from which the tuner's hold loses ground

static inline int rs_fr_fast(float dt) { return dt < RS_FR_FAST_DT; }

// The calibration window, within a player's buffer
static inline uint32_t rs_fr_window(uint32_t frames) {
  return frames < RS_FR_WINDOW_MIN ? RS_FR_WINDOW_MIN : frames > RS_FR_WINDOW_MAX ? RS_FR_WINDOW_MAX : frames;
}

// A step the game takes once a frame, as much of it as this frame is of a 60 Hz one
static inline float rs_fr_step(float step, float dt) {
  if (!rs_fr_fast(dt)) return step;
  return dt > 0 ? step * dt * 60.0f : 0.0f;
}

// What the hooks keep for one of the game's objects (a tuner, a menu)
typedef struct {
  const void *who;
  float t;
} rs_fr_slot;

// Whether a 60 Hz game would have made this call: every call at 60 Hz and below, one in each 1/60 s above
static inline int rs_fr_gate(rs_fr_slot *s, float dt) {
  if (!rs_fr_fast(dt)) {
    s->t = 0;
    return 1;
  }
  s->t += dt;
  if (s->t + RS_FR_TOL < RS_FR_REF_DT) return 0;
  s->t -= RS_FR_REF_DT;
  if (s->t > RS_FR_REF_DT) s->t = RS_FR_REF_DT;
  return 1;
}

// The misses a 60 Hz game would have counted, of `misses` calls in a row: the first, and one for each 1/60 s since
static inline uint32_t rs_fr_misses(rs_fr_slot *s, uint32_t misses, float dt) {
  if (misses <= 1) {
    s->t = 0;
    return misses;
  }
  s->t += dt;
  if (!rs_fr_fast(dt)) return misses;
  uint32_t at60 = 1 + (uint32_t)((s->t + RS_FR_TOL) / RS_FR_REF_DT);
  return at60 < misses ? at60 : misses;
}

// A few objects at once: two players' tuners at most
#define RS_FR_SLOTS 4
typedef struct {
  rs_fr_slot slot[RS_FR_SLOTS];
  unsigned next;
} rs_fr_slots;

static inline rs_fr_slot *rs_fr_slot_of(rs_fr_slots *t, const void *who) {
  for (int i = 0; i < RS_FR_SLOTS; i++)
    if (t->slot[i].who == who) return &t->slot[i];
  rs_fr_slot *s = &t->slot[t->next++ % RS_FR_SLOTS];
  s->who = who;
  s->t = 0;
  return s;
}

// Adds the hooks, before the game runs: the number of points hooked, or -1 with RS_MODS_FRAME_RATE=0.
#define RS_FR_POINTS 8
int rs_framerate_init(void);

#endif
