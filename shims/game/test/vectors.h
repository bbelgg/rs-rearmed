// Deterministic test inputs shared by the x86_64 differential test (which runs
// the original code) and the arm64 native test (which checks the rewrites
// against the golden file the former writes).
#ifndef RS_GAME_VECTORS_H
#define RS_GAME_VECTORS_H

#include <stddef.h>
#include <stdint.h>

static uint64_t vec_rng = 0x9e3779b97f4a7c15ull;
static inline uint32_t vec_next(void) {  // xorshift64*
  vec_rng ^= vec_rng >> 12;
  vec_rng ^= vec_rng << 25;
  vec_rng ^= vec_rng >> 27;
  return (uint32_t)((vec_rng * 0x2545f4914f6cdd1dull) >> 32);
}

// Wwise-style names: identifiers, mixed case, digits, '_' and some high bytes.
static inline void vec_name(char *out, size_t cap, int i) {
  static const char *fixed[] = {"", "Play_Music", "PLAY_MUSIC", "play_music", "Stop_All",
                                "Set_State_Menu", "RTPC_Guitar_Volume", "Init.bnk",
                                "\xc3\x84\xc3\xa4kk\xc3\xb6nen", "@[`{"};
  size_t nfixed = sizeof fixed / sizeof *fixed;
  if ((size_t)i < nfixed) {
    size_t k = 0;
    for (; fixed[i][k] && k + 1 < cap; k++) out[k] = fixed[i][k];
    out[k] = 0;
    return;
  }
  size_t len = vec_next() % (cap < 200 ? cap - 1 : 200);
  for (size_t k = 0; k < len; k++) {
    uint32_t r = vec_next() % 100;
    out[k] = r < 40 ? 'a' + vec_next() % 26 : r < 75 ? 'A' + vec_next() % 26
           : r < 85 ? '0' + vec_next() % 10 : r < 92 ? '_' : (char)(0x80 + vec_next() % 0x7f);
  }
  out[len] = 0;
}

// Compressible pseudo-random bytes (runs + noise), like game asset data.
static inline void vec_bytes(uint8_t *out, size_t n) {
  size_t k = 0;
  while (k < n) {
    uint8_t b = (uint8_t)vec_next();
    size_t run = (vec_next() % 4 == 0) ? 1 + vec_next() % 64 : 1;
    for (size_t j = 0; j < run && k < n; j++) out[k++] = b;
  }
}

#define VEC_NAMES 5000
#define VEC_BLOBS 200
#define VEC_BLOB_MAX 300000

// One zlib case. Both tests must call this in the same order (it drives the RNG).
typedef struct {
  size_t n;      // uncompressed size
  int level;     // compress2 level
  int corrupt;   // flip one byte of the compressed stream
  uint32_t pos;  // which byte (mod compressed size)
} vec_blob;

static inline void vec_blob_case(int i, uint8_t *blob, vec_blob *c) {
  c->n = i < 3 ? (size_t)i : vec_next() % VEC_BLOB_MAX;
  vec_bytes(blob, c->n);
  c->level = 1 + vec_next() % 9;
  c->corrupt = vec_next() % 8 == 0;
  c->pos = vec_next();
}

#endif
