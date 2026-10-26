// AES for the game's crypto (rs_crypto.h): the Rijndael reference algorithm
// (rijndael-alg-fst.c v3.0 by Rijmen, Bosselaers and Barreto, public domain;
// the game's copy is 0x100001b40-0x100002df0), the Crypto++ HexDecoder rules,
// and the CTR and CFB modes of Crypto++'s wrappers 0x101376328/0x101376f8f.
// The T-tables are built once from the S-box instead of being spelled out.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_internal.h"
#include "rs_crypto.h"

static uint32_t Te0[256], Te1[256], Te2[256], Te3[256], Te4[256];
static uint32_t Td0[256], Td1[256], Td2[256], Td3[256], Td4[256];
static const uint32_t rcon[10] = {0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
                                  0x20000000, 0x40000000, 0x80000000, 0x1b000000, 0x36000000};
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

static uint8_t xtime(uint8_t a) { return (uint8_t)((a << 1) ^ (a & 0x80 ? 0x1b : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b) {
    if (b & 1) r ^= a;
    a = xtime(a);
    b >>= 1;
  }
  return r;
}
static uint32_t ror8(uint32_t x) { return x >> 8 | x << 24; }

static void build_tables(void) {
  // S-box: multiplicative inverse in GF(2^8) followed by the affine map.
  uint8_t sbox[256], inv[256];
  uint8_t p = 1, q = 1;
  do {
    p = (uint8_t)(p ^ xtime(p));  // p *= 3
    q ^= q << 1;                  // q /= 3
    q ^= q << 2;
    q ^= q << 4;
    if (q & 0x80) q ^= 0x09;
    uint8_t x = (uint8_t)(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^ (q << 4 | q >> 4));
    sbox[p] = x ^ 0x63;
  } while (p != 1);
  sbox[0] = 0x63;
  for (int i = 0; i < 256; i++) inv[sbox[i]] = (uint8_t)i;
  for (int i = 0; i < 256; i++) {
    uint8_t s = sbox[i], s2 = xtime(s), s3 = s2 ^ s;
    Te0[i] = (uint32_t)s2 << 24 | (uint32_t)s << 16 | (uint32_t)s << 8 | s3;
    Te1[i] = ror8(Te0[i]);
    Te2[i] = ror8(Te1[i]);
    Te3[i] = ror8(Te2[i]);
    Te4[i] = (uint32_t)s * 0x01010101u;
    uint8_t t = inv[i];
    Td0[i] = (uint32_t)gmul(t, 14) << 24 | (uint32_t)gmul(t, 9) << 16 | (uint32_t)gmul(t, 13) << 8 | gmul(t, 11);
    Td1[i] = ror8(Td0[i]);
    Td2[i] = ror8(Td1[i]);
    Td3[i] = ror8(Td2[i]);
    Td4[i] = (uint32_t)t * 0x01010101u;
  }
}
static void tables(void) { pthread_once(&tables_once, build_tables); }

#define GETU32(p) ((uint32_t)(p)[0] << 24 ^ (uint32_t)(p)[1] << 16 ^ (uint32_t)(p)[2] << 8 ^ (uint32_t)(p)[3])
#define PUTU32(c, v)                 \
  do {                               \
    (c)[0] = (uint8_t)((v) >> 24);   \
    (c)[1] = (uint8_t)((v) >> 16);   \
    (c)[2] = (uint8_t)((v) >> 8);    \
    (c)[3] = (uint8_t)(v);           \
  } while (0)

static uint32_t sub_rot(uint32_t t) {
  return (Te4[(t >> 16) & 0xff] & 0xff000000) ^ (Te4[(t >> 8) & 0xff] & 0x00ff0000) ^
         (Te4[t & 0xff] & 0x0000ff00) ^ (Te4[t >> 24] & 0x000000ff);
}
static uint32_t sub(uint32_t t) {
  return (Te4[t >> 24] & 0xff000000) ^ (Te4[(t >> 16) & 0xff] & 0x00ff0000) ^
         (Te4[(t >> 8) & 0xff] & 0x0000ff00) ^ (Te4[t & 0xff] & 0x000000ff);
}

int rsg_rijndael_key_setup_enc(uint32_t rk[], const uint8_t key[], int key_bits) {
  tables();
  int i = 0;
  rk[0] = GETU32(key);
  rk[1] = GETU32(key + 4);
  rk[2] = GETU32(key + 8);
  rk[3] = GETU32(key + 12);
  if (key_bits == 128) {
    for (;;) {
      rk[4] = rk[0] ^ sub_rot(rk[3]) ^ rcon[i];
      rk[5] = rk[1] ^ rk[4];
      rk[6] = rk[2] ^ rk[5];
      rk[7] = rk[3] ^ rk[6];
      if (++i == 10) return 10;
      rk += 4;
    }
  }
  rk[4] = GETU32(key + 16);
  rk[5] = GETU32(key + 20);
  if (key_bits == 192) {
    for (;;) {
      rk[6] = rk[0] ^ sub_rot(rk[5]) ^ rcon[i];
      rk[7] = rk[1] ^ rk[6];
      rk[8] = rk[2] ^ rk[7];
      rk[9] = rk[3] ^ rk[8];
      if (++i == 8) return 12;
      rk[10] = rk[4] ^ rk[9];
      rk[11] = rk[5] ^ rk[10];
      rk += 6;
    }
  }
  rk[6] = GETU32(key + 24);
  rk[7] = GETU32(key + 28);
  if (key_bits == 256) {
    for (;;) {
      rk[8] = rk[0] ^ sub_rot(rk[7]) ^ rcon[i];
      rk[9] = rk[1] ^ rk[8];
      rk[10] = rk[2] ^ rk[9];
      rk[11] = rk[3] ^ rk[10];
      if (++i == 7) return 14;
      rk[12] = rk[4] ^ sub(rk[11]);
      rk[13] = rk[5] ^ rk[12];
      rk[14] = rk[6] ^ rk[13];
      rk[15] = rk[7] ^ rk[14];
      rk += 8;
    }
  }
  return 0;
}

static uint32_t inv_mix(uint32_t w) {
  return Td0[Te4[w >> 24] & 0xff] ^ Td1[Te4[(w >> 16) & 0xff] & 0xff] ^ Td2[Te4[(w >> 8) & 0xff] & 0xff] ^
         Td3[Te4[w & 0xff] & 0xff];
}

int rsg_rijndael_key_setup_dec(uint32_t rk[], const uint8_t key[], int key_bits) {
  int nr = rsg_rijndael_key_setup_enc(rk, key, key_bits);
  if (nr <= 0) return nr;
  for (int i = 0, j = 4 * nr; i < j; i += 4, j -= 4)
    for (int k = 0; k < 4; k++) {
      uint32_t t = rk[i + k];
      rk[i + k] = rk[j + k];
      rk[j + k] = t;
    }
  for (int i = 1; i < nr; i++)
    for (int k = 0; k < 4; k++) rk[4 * i + k] = inv_mix(rk[4 * i + k]);
  return nr;
}

void rsg_rijndael_encrypt(const uint32_t rk[], int nr, const uint8_t pt[16], uint8_t ct[16]) {
  tables();
  uint32_t s0 = GETU32(pt) ^ rk[0], s1 = GETU32(pt + 4) ^ rk[1], s2 = GETU32(pt + 8) ^ rk[2],
           s3 = GETU32(pt + 12) ^ rk[3], t0, t1, t2, t3;
  for (int r = nr >> 1;;) {
    t0 = Te0[s0 >> 24] ^ Te1[(s1 >> 16) & 0xff] ^ Te2[(s2 >> 8) & 0xff] ^ Te3[s3 & 0xff] ^ rk[4];
    t1 = Te0[s1 >> 24] ^ Te1[(s2 >> 16) & 0xff] ^ Te2[(s3 >> 8) & 0xff] ^ Te3[s0 & 0xff] ^ rk[5];
    t2 = Te0[s2 >> 24] ^ Te1[(s3 >> 16) & 0xff] ^ Te2[(s0 >> 8) & 0xff] ^ Te3[s1 & 0xff] ^ rk[6];
    t3 = Te0[s3 >> 24] ^ Te1[(s0 >> 16) & 0xff] ^ Te2[(s1 >> 8) & 0xff] ^ Te3[s2 & 0xff] ^ rk[7];
    rk += 8;
    if (--r == 0) break;
    s0 = Te0[t0 >> 24] ^ Te1[(t1 >> 16) & 0xff] ^ Te2[(t2 >> 8) & 0xff] ^ Te3[t3 & 0xff] ^ rk[0];
    s1 = Te0[t1 >> 24] ^ Te1[(t2 >> 16) & 0xff] ^ Te2[(t3 >> 8) & 0xff] ^ Te3[t0 & 0xff] ^ rk[1];
    s2 = Te0[t2 >> 24] ^ Te1[(t3 >> 16) & 0xff] ^ Te2[(t0 >> 8) & 0xff] ^ Te3[t1 & 0xff] ^ rk[2];
    s3 = Te0[t3 >> 24] ^ Te1[(t0 >> 16) & 0xff] ^ Te2[(t1 >> 8) & 0xff] ^ Te3[t2 & 0xff] ^ rk[3];
  }
#define FINAL_E(a, b, c, d, k)                                                                      \
  ((Te4[(a) >> 24] & 0xff000000) ^ (Te4[((b) >> 16) & 0xff] & 0x00ff0000) ^                         \
   (Te4[((c) >> 8) & 0xff] & 0x0000ff00) ^ (Te4[(d) & 0xff] & 0x000000ff) ^ (k))
  s0 = FINAL_E(t0, t1, t2, t3, rk[0]);
  s1 = FINAL_E(t1, t2, t3, t0, rk[1]);
  s2 = FINAL_E(t2, t3, t0, t1, rk[2]);
  s3 = FINAL_E(t3, t0, t1, t2, rk[3]);
  PUTU32(ct, s0);
  PUTU32(ct + 4, s1);
  PUTU32(ct + 8, s2);
  PUTU32(ct + 12, s3);
}

void rsg_rijndael_decrypt(const uint32_t rk[], int nr, const uint8_t ct[16], uint8_t pt[16]) {
  tables();
  uint32_t s0 = GETU32(ct) ^ rk[0], s1 = GETU32(ct + 4) ^ rk[1], s2 = GETU32(ct + 8) ^ rk[2],
           s3 = GETU32(ct + 12) ^ rk[3], t0, t1, t2, t3;
  for (int r = nr >> 1;;) {
    t0 = Td0[s0 >> 24] ^ Td1[(s3 >> 16) & 0xff] ^ Td2[(s2 >> 8) & 0xff] ^ Td3[s1 & 0xff] ^ rk[4];
    t1 = Td0[s1 >> 24] ^ Td1[(s0 >> 16) & 0xff] ^ Td2[(s3 >> 8) & 0xff] ^ Td3[s2 & 0xff] ^ rk[5];
    t2 = Td0[s2 >> 24] ^ Td1[(s1 >> 16) & 0xff] ^ Td2[(s0 >> 8) & 0xff] ^ Td3[s3 & 0xff] ^ rk[6];
    t3 = Td0[s3 >> 24] ^ Td1[(s2 >> 16) & 0xff] ^ Td2[(s1 >> 8) & 0xff] ^ Td3[s0 & 0xff] ^ rk[7];
    rk += 8;
    if (--r == 0) break;
    s0 = Td0[t0 >> 24] ^ Td1[(t3 >> 16) & 0xff] ^ Td2[(t2 >> 8) & 0xff] ^ Td3[t1 & 0xff] ^ rk[0];
    s1 = Td0[t1 >> 24] ^ Td1[(t0 >> 16) & 0xff] ^ Td2[(t3 >> 8) & 0xff] ^ Td3[t2 & 0xff] ^ rk[1];
    s2 = Td0[t2 >> 24] ^ Td1[(t1 >> 16) & 0xff] ^ Td2[(t0 >> 8) & 0xff] ^ Td3[t3 & 0xff] ^ rk[2];
    s3 = Td0[t3 >> 24] ^ Td1[(t2 >> 16) & 0xff] ^ Td2[(t1 >> 8) & 0xff] ^ Td3[t0 & 0xff] ^ rk[3];
  }
#define FINAL_D(a, b, c, d, k)                                                                      \
  ((Td4[(a) >> 24] & 0xff000000) ^ (Td4[((b) >> 16) & 0xff] & 0x00ff0000) ^                         \
   (Td4[((c) >> 8) & 0xff] & 0x0000ff00) ^ (Td4[(d) & 0xff] & 0x000000ff) ^ (k))
  s0 = FINAL_D(t0, t3, t2, t1, rk[0]);
  s1 = FINAL_D(t1, t0, t3, t2, rk[1]);
  s2 = FINAL_D(t2, t1, t0, t3, rk[2]);
  s3 = FINAL_D(t3, t2, t1, t0, rk[3]);
  PUTU32(pt, s0);
  PUTU32(pt + 4, s1);
  PUTU32(pt + 8, s2);
  PUTU32(pt + 12, s3);
}

// --- Crypto++ HexDecoder -------------------------------------------------------
size_t rsg_hex_decode(const char *hex, size_t n, uint8_t *out) {
  size_t o = 0;
  int half = -1;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)hex[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) continue;
    if (half < 0) {
      half = v;
    } else {
      out[o++] = (uint8_t)(half << 4 | v);
      half = -1;
    }
  }
  return o;  // a trailing odd digit is dropped
}

// Decodes an AES key like the wrappers. Returns Nr, or 0 for a length
// Crypto++'s Rijndael rejects (InvalidKeyLength).
static int aes_key_from_hex(const char *hex, size_t n, uint32_t rk[60]) {
  uint8_t *key = malloc(n / 2 + 1);
  size_t k = rsg_hex_decode(hex, n, key);
  int nr = k == 16 || k == 24 || k == 32 ? rsg_rijndael_key_setup_enc(rk, key, (int)k * 8) : 0;
  memset(key, 0, n / 2 + 1);
  free(key);
  return nr;
}

bool rsg_cryptopp_aes_ctr_decrypt(const uint8_t *data, uint64_t len, const char *key_hex,
                                  uint32_t key_hex_len, uint8_t *out, uint32_t out_len) {
  uint32_t rk[60];
  int nr = aes_key_from_hex(key_hex, key_hex_len, rk);
  if (!nr) return false;
  uint8_t ctr[16], ks[16];
  memcpy(ctr, data, 16);
  uint32_t n = (uint32_t)(len - 16);  // as the original: 32-bit length
  uint32_t m = n < out_len ? n : out_len;  // ArraySink keeps what fits
  const uint8_t *in = data + 16;
  for (uint32_t i = 0; i < m; i += 16) {
    rsg_rijndael_encrypt(rk, nr, ctr, ks);
    for (int b = 15; b >= 0 && ++ctr[b] == 0; b--) {
    }
    uint32_t c = m - i < 16 ? m - i : 16;
    for (uint32_t j = 0; j < c; j++) out[i + j] = in[i + j] ^ ks[j];
  }
  memset(rk, 0, sizeof rk);
  return true;
}

bool rsg_cryptopp_aes_cfb_decrypt(const uint8_t *in, uint32_t len, const char *key_hex,
                                  uint32_t key_hex_len, uint8_t *out, const uint8_t iv[16]) {
  uint32_t rk[60];
  int nr = aes_key_from_hex(key_hex, key_hex_len, rk);
  if (!nr) return false;
  uint8_t reg[16], ks[16];
  memcpy(reg, iv, 16);
  for (uint32_t i = 0; i < len; i += 16) {
    rsg_rijndael_encrypt(rk, nr, reg, ks);
    uint32_t c = len - i < 16 ? len - i : 16;
    for (uint32_t j = 0; j < c; j++) {
      uint8_t x = in[i + j];  // in may equal out
      out[i + j] = x ^ ks[j];
      reg[j] = x;
    }
  }
  memset(rk, 0, sizeof rk);
  return true;
}

void rsg_aes256_ecb_decrypt_blocks(const uint8_t key[32], uint8_t *buf, size_t len) {
  uint32_t rk[60];
  int nr = rsg_rijndael_key_setup_dec(rk, key, 256);
  for (size_t i = 0; i + 16 <= len; i += 16) {
    uint8_t t[16];
    memcpy(t, buf + i, 16);
    rsg_rijndael_decrypt(rk, nr, t, buf + i);
  }
  memset(rk, 0, sizeof rk);
}

void rsg_aes256_ecb_encrypt_blocks(const uint8_t key[32], uint8_t *buf, size_t len) {
  uint32_t rk[60];
  int nr = rsg_rijndael_key_setup_enc(rk, key, 256);
  for (size_t i = 0; i + 16 <= len; i += 16) {
    uint8_t t[16];
    memcpy(t, buf + i, 16);
    rsg_rijndael_encrypt(rk, nr, t, buf + i);
  }
  memset(rk, 0, sizeof rk);
}
