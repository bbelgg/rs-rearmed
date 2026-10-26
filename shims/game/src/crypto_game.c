// Game code around the crypto (rs_crypto.h): the BPE key unpacker
// 0x1001eaa70, the profile save/load block loops 0x100168c90/0x100168df0 and
// the package-config callback 0x1002a7550.
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "rs_crypto.h"

int rsg_bpe_expand(const uint8_t *src, int n, uint8_t *out) {
  if (n <= 0) return 0;
  const uint8_t *p = src, *end = src + n;
  uint8_t *o = out, left[256], right[256], stack[32];
  memset(right, 0, sizeof right);
  do {
    for (int i = 0; i < 256; i++) left[i] = (uint8_t)i;
    // Pair table: runs of literal/pair entries, a count byte > 127 skips.
    for (int c = 0;;) {
      int count = *p++;
      if (count > 127) {
        c += count - 127;
        count = 0;
        if (c == 256) break;
      }
      if (c + count > 255) goto done;  // the original writes past its tables here
      for (int i = 0; i <= count; i++, c++) {
        left[c] = *p++;
        if (c != left[c]) right[c] = *p++;
      }
      if (c == 256) break;
    }
    int size = p[0] << 8 | p[1], sp = 0;
    p += 2;
    for (;;) {
      int c;
      if (sp) {
        c = stack[--sp];
      } else {
        if (!size) break;
        size--;
        c = *p++;
      }
      if (c == left[c]) {
        *o++ = (uint8_t)~c;
      } else {
        if (sp > 30) goto done;  // the original's stack is 32 bytes and unchecked
        stack[sp++] = right[c];
        stack[sp++] = left[c];
      }
    }
  } while (p < end);
done:
  return (int)(o - out);
}

void rsg_profile_encrypt(uint8_t *buf, int *len, const uint8_t key[32], int (*rnd)(void)) {
  uint32_t left = (uint32_t)*len;
  if (left == 0) {
    *len = 16;
    return;
  }
  uint32_t rk[60];
  int nr = rsg_rijndael_key_setup_enc(rk, key, 256);
  for (uint8_t *p = buf; left;) {
    uint8_t t[16] = {0};
    uint32_t c = left < 16 ? left : 16;
    memcpy(t, p, c);
    if (c < 16) {
      t[c] = 0;
      for (uint32_t i = left + 1; i < 16; i++) t[i] = (uint8_t)rnd();
    }
    rsg_rijndael_encrypt(rk, nr, t, p);  // writes a whole block, padding included
    p += c;
    left -= c;
  }
  memset(rk, 0, sizeof rk);
  *len = (*len - 1) / 16 * 16 + 16;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int rsg_profile_decode(uint8_t *file, uint32_t size, const uint8_t key[32], bool from_cloud,
                       uint64_t user_id, uint8_t **out, uint32_t *out_len) {
  *out = NULL;
  *out_len = 0;
  if (size <= 15 || rd32(file) != 0x53415645u) return RSG_PROFILE_BAD;  // "EVAS"
  uint32_t version = rd32(file + 4);
  if (version >= 2) return RSG_PROFILE_TOO_NEW;
  if (version == 0) return RSG_PROFILE_BAD;
  if (from_cloud && ((uint64_t)rd32(file + 8) | (uint64_t)rd32(file + 12) << 32) != user_id)
    return RSG_PROFILE_OTHER_USER;
  if ((size & ~3u) == 16) return RSG_PROFILE_BAD;
  uint32_t body = size - 0x14;
  if (body & 15) return RSG_PROFILE_BAD;
  int32_t usize = (int32_t)rd32(file + 0x10);
  if (usize < 0) return RSG_PROFILE_BAD;  // the original would ask for a huge block
  rsg_aes256_ecb_decrypt_blocks(key, file + 0x14, body);
  uint8_t *dst = malloc(usize ? (size_t)usize : 1);
  uLongf dlen = (uLongf)usize;
  if (uncompress(dst, &dlen, file + 0x14, body) != Z_OK || dlen != (uLongf)usize) {
    free(dst);
    return RSG_PROFILE_BAD;
  }
  *out = dst;
  *out_len = (uint32_t)usize;
  return RSG_PROFILE_OK;
}

uint32_t rsg_pkgconfig_decrypt(uint8_t *buf, uint32_t len, const char *dsa_key_hex,
                               const uint8_t key[32], bool *verified) {
  bool ok = rsg_cryptopp_dsa_sha1_verify(buf, len, dsa_key_hex);
  if (verified) *verified = ok;
  uint32_t region = ok ? len - 0x38 : len;
  rsg_aes256_ecb_decrypt_blocks(key, buf, region);
  uint32_t t = 0;
  while (t < region && buf[t]) t++;
  return t;
}
