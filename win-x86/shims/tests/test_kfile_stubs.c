// What win32_kfile.c needs from the rest of the seam, for test_kfile: no frame log, ASCII strings.
#include <string.h>

#include "win32_shims.h"

int lp_frame_on = 0;
void lp_frame_event(int kind, uint64_t ns, uint64_t amount) {}
uint64_t lp_frame_now(void) { return 0; }

char *w32_wstr(uint32_t g, char *out, size_t n) {
  size_t k = 0;
  for (uint16_t ch; k < n - 1 && (ch = ((uint16_t *)RC32_H(g))[k]); k++) out[k] = (char)ch;
  out[k] = 0;
  return out;
}
uint32_t w32_put_wstr(uint32_t g, uint32_t cap, const char *s) {
  uint32_t k = (uint32_t)strlen(s);
  if (!g) return k;
  if (k >= cap) k = cap ? cap - 1 : 0;
  for (uint32_t i = 0; i < k; i++) ((uint16_t *)RC32_H(g))[i] = (uint8_t)s[i];
  if (cap) ((uint16_t *)RC32_H(g))[k] = 0;
  return k;
}
