// SSE helpers the 32-bit front end needs beyond rc_sse.h (kept apart so 64-bit output and its
// headers stay unchanged).
#ifndef RC32_SSE_H
#define RC32_SSE_H
#include "rc_sse.h"

// pmaddubsw (SSSE3): unsigned bytes of a times signed bytes of b, adjacent pairs added with
// signed saturation to words.
static inline rc_v128 rc32_pmaddubsw(rc_v128 a, rc_v128 b) {
  uint8_t x[16], y[16];
  int16_t r[8];
  memcpy(x, &a, 16);
  memcpy(y, &b, 16);
  for (int k = 0; k < 8; k++) {
    int32_t s = (int32_t)x[2 * k] * (int8_t)y[2 * k] + (int32_t)x[2 * k + 1] * (int8_t)y[2 * k + 1];
    r[k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
  }
  rc_v128 v;
  memcpy(&v, r, 16);
  return v;
}

#endif
