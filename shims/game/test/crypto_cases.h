// Crypto differential test cases (include/rs_crypto.h), shared by
// harness/difftest.c (the original x86_64 code vs the rewrites, under
// Rosetta) and test/native_test.c (the arm64 rewrites vs the golden file).
//
// Like the efd tests, a driver calls everything through a table of function
// pointers with the originals' signatures (crypto_api) and writes what it
// sees into a text transcript (efd_fakes.h): return values, lengths and CRCs
// of outputs, log messages. Transcripts never contain key material: the keys
// stay in memory (crypto_keys), read from the game image at runtime.
//
// Groups (cases seed their own RNG from (group, index)):
//   bpe        the three key blobs (length and hex check only) + 2,000 generated streams
//   rijndael   FIPS-197 vectors, unsupported key sizes, 1,000 random keys x 8 blocks
//   ctr        Crypto++ AES-CTR wrapper: key sizes valid and not, hex formatting, lengths, sinks
//   cfb        Crypto++ AES-CFB wrapper (the game's IV), the same variations
//   dsa        Crypto++ DSA2<SHA1> verify with keys generated here (2048/224, 1024/160,
//              2048/256), a test-only signer, corrupted messages, signatures and keys
//   psarc      the real PSARC tables of contents of the installed game (CFB, game key)
//   sng        real .sng files from the game's archives: signature (game DSA key), payload
//              (AES-CTR, game key) and the zlib stream inside; 1 in 4 with a flipped bit
//   profile    synthetic profile saves: encrypted by the save function (srand seeded),
//              then loaded; damaged headers, sizes and bodies
//   pkgconfig  the real pkgconfig.ini (static.psarc) + generated ones (unsigned: logged)
//   preview    AES-256-ECB blocks with the DLC preview-metadata key
//
// Game data is read from the install (read-only) and never copied into the
// repository; without it the psarc/sng/pkgconfig-real cases are absent on
// both sides.
#ifndef RS_GAME_CRYPTO_CASES_H
#define RS_GAME_CRYPTO_CASES_H

#include <CommonCrypto/CommonDigest.h>
#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../src/crypto_internal.h"
#include "efd_fakes.h"
#include "rs_crypto.h"

typedef struct {
  int (*ks_enc)(uint32_t *, const uint8_t *, int);
  int (*ks_dec)(uint32_t *, const uint8_t *, int);
  void (*enc)(const uint32_t *, int, const uint8_t *, uint8_t *);
  void (*dec)(const uint32_t *, int, const uint8_t *, uint8_t *);
  int (*bpe)(const uint8_t *, int, uint8_t *);
  bool (*ctr)(const uint8_t *, uint64_t, const char *, uint32_t, uint8_t *, uint32_t);
  bool (*cfb)(const uint8_t *, uint32_t, const char *, uint32_t, uint8_t *);  // the game's IV
  bool (*dsa)(const uint8_t *, uint32_t, const char *);
  void (*profile_encrypt)(uint8_t *, int *);  // the game's key, rand()
  // Profile load: the callback's fail flag and code, and on success a
  // malloc'd copy of the data.
  void (*profile_load)(const uint8_t *, uint32_t, int *fail, int *code, uint8_t **out, uint32_t *len);
  // Package config: returns the text length, *out = malloc'd decrypted buffer.
  uint32_t (*pkgconfig)(const uint8_t *, uint32_t, uint8_t **out);
  void (*preview_ecb)(uint8_t *, size_t);  // decrypt whole blocks, the preview key
} crypto_api;

// --- keys, from the image ------------------------------------------------------
typedef struct {
  char sng_dsa[0x691], sng_aes[0x41], pkg_dsa[0x68f], psarc_hex[0x41];
  uint8_t psarc_iv[16], profile[32], pkg_aes[32], preview[32];
  int blob_len[3];  // unpacked lengths of the three BPE blobs
} crypto_keys;
static crypto_keys ck;

// at(vm) returns a pointer to the image's bytes at an unslid address.
static void crypto_keys_load(const uint8_t *(*at)(uint64_t)) {
  memset(&ck, 0, sizeof ck);
  static const struct {
    uint64_t len, blob;
    size_t off, cap;
  } b[3] = {{RS_KEY_SNG_DSA_BLOB_LEN, RS_KEY_SNG_DSA_BLOB, offsetof(crypto_keys, sng_dsa), 0x691},
            {RS_KEY_SNG_AES_BLOB_LEN, RS_KEY_SNG_AES_BLOB, offsetof(crypto_keys, sng_aes), 0x41},
            {RS_KEY_PKG_DSA_BLOB_LEN, RS_KEY_PKG_DSA_BLOB, offsetof(crypto_keys, pkg_dsa), 0x68f}};
  for (int i = 0; i < 3; i++) {
    uint32_t n;
    memcpy(&n, at(b[i].len), 4);
    uint8_t tmp[0x800];
    memset(tmp, 0, sizeof tmp);
    int r = rsg_bpe_expand(at(b[i].blob), (int)n, tmp);
    ck.blob_len[i] = r;
    memcpy((char *)&ck + b[i].off, tmp, b[i].cap - 1);  // NUL-terminated
    memset(tmp, 0, sizeof tmp);
  }
  memcpy(ck.psarc_hex, at(RS_KEY_PSARC_HEX), 0x41);
  memcpy(ck.psarc_iv, at(RS_KEY_PSARC_IV), 16);
  memcpy(ck.profile, at(RS_KEY_PROFILE), 32);
  memcpy(ck.pkg_aes, at(RS_KEY_PKG_AES), 32);
  memcpy(ck.preview, at(RS_KEY_PREVIEW), 32);
}

// --- helpers -------------------------------------------------------------------
static uint64_t cr_rng;
static uint32_t cr_next(void) {
  cr_rng ^= cr_rng >> 12;
  cr_rng ^= cr_rng << 25;
  cr_rng ^= cr_rng >> 27;
  return (uint32_t)((cr_rng * 0x2545f4914f6cdd1dull) >> 32);
}
static void cr_seed(int g, int i) {
  cr_rng = 0x6a09e667f3bcc909ull ^ ((uint64_t)(g + 1) << 40) ^ (uint64_t)(i + 1) * 0x9e3779b97f4a7c15ull;
  for (int k = 0; k < 4; k++) cr_next();
}
static void cr_bytes(uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)cr_next();
}
static unsigned long cr_crc(const void *p, size_t n) { return crc32(0, (const uint8_t *)p, (uInt)n); }

// Hex text for a key, in a few spellings Crypto++'s HexDecoder must read the
// same way. Returns the length; out is NUL-terminated.
static size_t cr_hex(char *out, const uint8_t *k, size_t n, int style) {
  static const char lo[] = "0123456789abcdef", up[] = "0123456789ABCDEF", junk[] = " \n\t:-gGxz";
  size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    const char *d = style == 1 ? up : style == 2 && (cr_next() & 1) ? up : lo;
    if (style == 2 && cr_next() % 5 == 0) out[o++] = junk[cr_next() % (sizeof junk - 1)];
    out[o++] = d[k[i] >> 4];
    if (style == 2 && cr_next() % 9 == 0) out[o++] = junk[cr_next() % (sizeof junk - 1)];
    out[o++] = d[k[i] & 15];
  }
  if (style == 3) out[o++] = lo[cr_next() % 16];  // odd digit: dropped
  out[o] = 0;
  return o;
}

// A zlib stream made of stored blocks: the same bytes on every platform (the
// system deflate may differ between architectures).
static size_t cr_zlib_stored(const uint8_t *in, size_t n, uint8_t *out) {
  size_t o = 0;
  out[o++] = 0x78;
  out[o++] = 0x01;
  size_t i = 0;
  do {
    size_t c = n - i > 65535 ? 65535 : n - i;
    out[o++] = i + c == n;
    out[o++] = (uint8_t)c;
    out[o++] = (uint8_t)(c >> 8);
    out[o++] = (uint8_t)~c;
    out[o++] = (uint8_t)(~c >> 8);
    memcpy(out + o, in + i, c);
    o += c;
    i += c;
  } while (i < n);
  unsigned long a = adler32(1, in, (uInt)n);
  out[o++] = (uint8_t)(a >> 24);
  out[o++] = (uint8_t)(a >> 16);
  out[o++] = (uint8_t)(a >> 8);
  out[o++] = (uint8_t)a;
  return o;
}

// --- BPE: an encoder for test streams --------------------------------------------
// Produces a valid Gage stream whose expansion, NOTed, is `plain`: a random
// acyclic pair table (pairs of earlier codes, depth <= 6) and a body of codes.
static int cr_bpe_gen(uint8_t *out, uint8_t *plain, int *plain_len) {
  int o = 0, pl = 0;
  int blocks = 1 + (int)(cr_next() % 3);
  for (int b = 0; b < blocks; b++) {
    uint8_t left[256], right[256], depth[256];
    for (int c = 0; c < 256; c++) left[c] = (uint8_t)c, depth[c] = 0;
    // Codes >= 128 + offset become pairs of codes with smaller depth.
    int first_pair = 128 + (int)(cr_next() % 96);
    for (int c = first_pair; c < 256; c++) {
      if (cr_next() % 3 == 0) continue;  // stays a literal
      int a = (int)(cr_next() % (unsigned)c), d = (int)(cr_next() % (unsigned)c);
      if (depth[a] >= 6 || depth[d] >= 6) continue;
      left[c] = (uint8_t)a;
      right[c] = (uint8_t)d;
      depth[c] = (uint8_t)(1 + (depth[a] > depth[d] ? depth[a] : depth[d]));
    }
    // Pair table: literal runs skipped with counts > 127, else explicit entries.
    for (int c = 0; c < 256;) {
      int run = 0;
      while (c + run < 256 && left[c + run] == c + run && run < 128) run++;
      if (run > 0 && (cr_next() % 2 || c + run == 256)) {
        // A skip is followed by one entry (Gage: count becomes 0, the loop reads 1).
        out[o++] = (uint8_t)(127 + run);
        c += run;
        if (c < 256) {
          out[o++] = left[c];
          if (left[c] != c) out[o++] = right[c];
          c++;
        }
        continue;
      }
      int cnt = 1 + (int)(cr_next() % 8);
      if (c + cnt > 256) cnt = 256 - c;
      out[o++] = (uint8_t)(cnt - 1);
      for (int k = 0; k < cnt; k++, c++) {
        out[o++] = left[c];
        if (left[c] != c) out[o++] = right[c];
      }
    }
    int size = (int)(cr_next() % 300);
    out[o++] = (uint8_t)(size >> 8);
    out[o++] = (uint8_t)size;
    for (int k = 0; k < size; k++) {
      int c = (int)(cr_next() % 256);
      out[o++] = (uint8_t)c;
      // expand for the reference plaintext
      uint8_t st[64];
      int sp = 0;
      st[sp++] = (uint8_t)c;
      while (sp) {
        int x = st[--sp];
        if (left[x] == x) plain[pl++] = (uint8_t)~x;
        else st[sp++] = right[x], st[sp++] = left[x];
      }
    }
  }
  *plain_len = pl;
  return o;
}

// --- DSA: test keys and a signer -------------------------------------------------
static uint32_t cr_small_primes[550];
static int cr_nsmall;
static void cr_primes_init(void) {
  if (cr_nsmall) return;
  for (uint32_t n = 3; cr_nsmall < 550; n += 2) {
    int p = 1;
    for (int i = 0; i < cr_nsmall && cr_small_primes[i] * cr_small_primes[i] <= n; i++)
      if (n % cr_small_primes[i] == 0) p = 0;
    if (p) cr_small_primes[cr_nsmall++] = n;
  }
}
static uint32_t bn_mod32(const rsg_bn *a, uint32_t m) {
  unsigned __int128 r = 0;
  for (int i = a->n - 1; i >= 0; i--) r = ((r << 64) | a->d[i]) % m;
  return (uint32_t)r;
}
static void bn_rand_bits(rsg_bn *a, int bits, int top2) {
  memset(a, 0, sizeof *a);
  for (int i = 0; i < (bits + 63) / 64; i++) a->d[i] = (uint64_t)cr_next() << 32 | cr_next();
  int w = (bits - 1) / 64, s = (bits - 1) % 64;
  a->d[w] &= s == 63 ? ~0ull : (2ull << s) - 1;
  a->d[w] |= 1ull << s;
  if (top2 && bits > 1) a->d[(bits - 2) / 64] |= 1ull << ((bits - 2) % 64);
  a->n = w + 1;
}
static bool bn_mr(const rsg_bn *n, int rounds) {
  rsg_bn one, nm1, d, x;
  rsg_bn_set_u64(&one, 1);
  rsg_bn_sub(&nm1, n, &one);
  int s = 0;
  d = nm1;
  while (!rsg_bn_is_odd(&d)) rsg_bn_shr(&d, &d, 1), s++;
  for (int r = 0; r < rounds; r++) {
    rsg_bn a;
    rsg_bn_set_u64(&a, 2 + (uint64_t)cr_next() % 1000000);
    rsg_bn_modexp(&x, &a, &d, n);
    if (rsg_bn_cmp(&x, &one) == 0 || rsg_bn_cmp(&x, &nm1) == 0) continue;
    int ok = 0;
    for (int k = 1; k < s && !ok; k++) {
      rsg_bn_modmul(&x, &x, &x, n);
      if (rsg_bn_cmp(&x, &nm1) == 0) ok = 1;
    }
    if (!ok) return false;
  }
  return true;
}
static bool bn_sieve_ok(const rsg_bn *n) {
  for (int i = 0; i < cr_nsmall; i++)
    if (bn_mod32(n, cr_small_primes[i]) == 0) return false;
  return true;
}

typedef struct {
  rsg_bn p, q, g, y, x;
  int L, N;
  uint8_t der[1200];
  size_t der_len;
} cr_dsa_key;

static size_t der_hdr(uint8_t *o, uint8_t tag, size_t len, int longform) {
  size_t k = 0;
  o[k++] = tag;
  if (len < 128 && !longform) {
    o[k++] = (uint8_t)len;
  } else {
    int nb = longform ? 4 : len < 256 ? 1 : 2;
    o[k++] = (uint8_t)(0x80 | nb);
    for (int i = nb - 1; i >= 0; i--) o[k++] = (uint8_t)(len >> (8 * i));
  }
  return k;
}
// INTEGER, minimal (a 0 byte when the top bit is set), or with one extra
// leading 0 (valid BER, not DER).
static size_t der_int(uint8_t *o, const rsg_bn *v, int extra_zero) {
  size_t vlen = (size_t)rsg_bn_bytes(v);
  uint8_t body[600];
  size_t b = 0;
  if (extra_zero) body[b++] = 0;
  uint8_t be[600];
  rsg_bn_to_be(v, be, vlen);
  if (vlen == 0 || (be[0] & 0x80)) body[b++] = 0;
  memcpy(body + b, be, vlen);
  b += vlen;
  size_t h = der_hdr(o, 0x02, b, 0);
  memcpy(o + h, body, b);
  return h + b;
}

// Variants of the key's encoding (all but the "bad" ones decode to the same key).
enum { KEY_PLAIN, KEY_LONGFORM, KEY_PADDED_INT, KEY_TRAILING, KEY_BAD_OID, KEY_BAD_UNUSED,
       KEY_NO_PARAMS, KEY_TRUNCATED, KEY_PG_ONLY, KEY_VARIANTS };
static size_t cr_dsa_der(const cr_dsa_key *k, int variant, uint8_t *o) {
  static const uint8_t oid[] = {0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x01};
  uint8_t params[1000], ints[1000], alg[1200], bits[700], spki[2000];
  size_t ni = 0;
  int lf = variant == KEY_LONGFORM, pad = variant == KEY_PADDED_INT;
  ni += der_int(ints + ni, &k->p, pad);
  if (variant == KEY_PG_ONLY) {
    ni += der_int(ints + ni, &k->g, 0);
  } else {
    ni += der_int(ints + ni, &k->q, 0);
    ni += der_int(ints + ni, &k->g, 0);
  }
  size_t np = der_hdr(params, 0x30, ni, lf);
  memcpy(params + np, ints, ni);
  np += ni;
  size_t na = 0;
  uint8_t algbody[1200];
  memcpy(algbody, oid, sizeof oid);
  if (variant == KEY_BAD_OID) algbody[sizeof oid - 1] = 0x03;  // 1.2.840.10040.4.3 (dsa-with-sha1)
  size_t nab = sizeof oid;
  if (variant != KEY_NO_PARAMS) {
    memcpy(algbody + nab, params, np);
    nab += np;
  }
  na = der_hdr(alg, 0x30, nab, lf);
  memcpy(alg + na, algbody, nab);
  na += nab;
  uint8_t yint[600];
  size_t ny = der_int(yint, &k->y, 0);
  size_t nb = der_hdr(bits, 0x03, ny + 1, lf);
  bits[nb++] = variant == KEY_BAD_UNUSED ? 1 : 0;
  memcpy(bits + nb, yint, ny);
  nb += ny;
  size_t ns = der_hdr(spki, 0x30, na + nb, lf);
  memcpy(spki + ns, alg, na);
  ns += na;
  memcpy(spki + ns, bits, nb);
  ns += nb;
  memcpy(o, spki, ns);
  if (variant == KEY_TRAILING) {
    memcpy(o + ns, "\x05\x00\xff\xff", 4);
    ns += 4;
  }
  if (variant == KEY_TRUNCATED) ns = 1 + cr_next() % (ns - 1);
  return ns;
}

static void cr_dsa_keygen(cr_dsa_key *k, int L, int N) {
  cr_primes_init();
  k->L = L;
  k->N = N;
  do bn_rand_bits(&k->q, N, 0), k->q.d[0] |= 1;
  while (!bn_sieve_ok(&k->q) || !bn_mr(&k->q, 8));
  uint32_t qm[550];
  for (int i = 0; i < cr_nsmall; i++) qm[i] = bn_mod32(&k->q, cr_small_primes[i]);
  rsg_bn kk, two, one, t;
  rsg_bn_set_u64(&two, 2);
  rsg_bn_set_u64(&one, 1);
  for (;;) {
    bn_rand_bits(&kk, L - N - 1, 1);
    int ok = 1;
    for (int i = 0; i < cr_nsmall && ok; i++) {
      uint64_t s = cr_small_primes[i];
      if ((2 * qm[i] % s * bn_mod32(&kk, (uint32_t)s) + 1) % s == 0) ok = 0;
    }
    if (!ok) continue;
    rsg_bn_mul(&t, &k->q, &kk);
    rsg_bn_mul(&t, &t, &two);
    rsg_bn_add(&k->p, &t, &one);
    if (rsg_bn_bits(&k->p) != L) continue;
    if (bn_mr(&k->p, 1) && bn_mr(&k->p, 2)) break;
  }
  rsg_bn e, pm1, h;
  rsg_bn_sub(&pm1, &k->p, &one);
  rsg_bn_divmod(&e, NULL, &pm1, &k->q);
  for (uint64_t hv = 2;; hv++) {
    rsg_bn_set_u64(&h, hv);
    rsg_bn_modexp(&k->g, &h, &e, &k->p);
    if (rsg_bn_cmp(&k->g, &one) != 0) break;
  }
  bn_rand_bits(&k->x, N - 1, 0);
  rsg_bn_modexp(&k->y, &k->g, &k->x, &k->p);
}

// Appends the signature (r || s) of msg[0..n) to msg[n..]; returns its length.
static size_t cr_dsa_sign(const cr_dsa_key *k, uint8_t *msg, size_t n) {
  uint8_t h[20];
  CC_SHA1(msg, (CC_LONG)n, h);
  rsg_bn e, kk, r, s, kinv, xr, sum, one;
  rsg_bn_from_be(&e, h, 20);  // bits(q) >= 160 for every test key
  rsg_bn_set_u64(&one, 1);
  size_t qb = (size_t)rsg_bn_bytes(&k->q);
  for (;;) {
    bn_rand_bits(&kk, k->N - 1, 0);
    rsg_bn gk;
    rsg_bn_modexp(&gk, &k->g, &kk, &k->p);
    rsg_bn_divmod(NULL, &r, &gk, &k->q);
    if (r.n == 0) continue;
    rsg_bn_modinv(&kinv, &kk, &k->q);
    rsg_bn_modmul(&xr, &k->x, &r, &k->q);
    rsg_bn_add(&sum, &e, &xr);
    rsg_bn_modmul(&s, &kinv, &sum, &k->q);
    if (s.n) break;
  }
  rsg_bn_to_be(&r, msg + n, qb);
  rsg_bn_to_be(&s, msg + n + qb, qb);
  return 2 * qb;
}

#define CR_DSA_KEYS 3
static cr_dsa_key cr_keys[CR_DSA_KEYS];
static void cr_dsa_keys_init(void) {
  static const int sizes[CR_DSA_KEYS][2] = {{2048, 224}, {1024, 160}, {2048, 256}};
  for (int i = 0; i < CR_DSA_KEYS; i++) {
    cr_seed(1000 + i, 0);
    cr_dsa_keygen(&cr_keys[i], sizes[i][0], sizes[i][1]);
    cr_keys[i].der_len = cr_dsa_der(&cr_keys[i], KEY_PLAIN, cr_keys[i].der);
  }
}

// --- the installed game's archives -----------------------------------------------
typedef struct {
  char path[1024];
} cr_path;
typedef struct {
  int psarc;
  uint32_t entry;
} cr_ref;
static cr_path *cr_psarcs;
static int cr_npsarcs;
static cr_ref *cr_sngs;
static int cr_nsngs;
static cr_ref cr_pkg = {-1, 0};

static int cr_cmp_path(const void *a, const void *b) { return strcmp(((const cr_path *)a)->path, ((const cr_path *)b)->path); }
static void cr_add_dir(const char *dir, int dlc) {
  DIR *d = opendir(dir);
  if (!d) return;
  int first = cr_npsarcs;
  struct dirent *e;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n < 7 || strcmp(e->d_name + n - 6, ".psarc") != 0) continue;
    if (dlc && !(n > 8 && (strcmp(e->d_name + n - 8, "_m.psarc") == 0 || strcmp(e->d_name + n - 8, "_p.psarc") == 0)))
      continue;
    cr_psarcs = realloc(cr_psarcs, sizeof *cr_psarcs * (size_t)(cr_npsarcs + 1));
    snprintf(cr_psarcs[cr_npsarcs++].path, sizeof(cr_path), "%s/%s", dir, e->d_name);
  }
  closedir(d);
  qsort(cr_psarcs + first, (size_t)(cr_npsarcs - first), sizeof *cr_psarcs, cr_cmp_path);
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be40(const uint8_t *p) { return (uint64_t)p[0] << 32 | be32(p + 1); }

// Reads the header and the raw (encrypted) TOC. Returns 0 on success.
static int cr_psarc_toc(const char *path, uint8_t hdr[32], uint8_t **toc, uint32_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  int r = -1;
  if (fread(hdr, 1, 32, f) == 32 && memcmp(hdr, "PSAR", 4) == 0) {
    uint32_t tl = be32(hdr + 12);
    if (tl > 32 && tl < (64u << 20)) {
      *n = tl - 32;
      *toc = malloc(*n);
      if (fread(*toc, 1, *n, f) == *n) r = 0;
      else free(*toc);
    }
  }
  fclose(f);
  return r;
}

typedef struct {
  uint8_t hdr[32], *toc;  // decrypted
  uint32_t toc_len, nent, esize, bsize;
  uint32_t *zs;
  uint32_t nz;
} cr_psarc;

static int cr_psarc_open(const char *path, cr_psarc *a) {
  memset(a, 0, sizeof *a);
  uint8_t *raw;
  if (cr_psarc_toc(path, a->hdr, &raw, &a->toc_len)) return -1;
  a->toc = malloc(a->toc_len);
  if (be32(a->hdr + 28) & 4) rsg_cryptopp_aes_cfb_decrypt(raw, a->toc_len, ck.psarc_hex, 0x41, a->toc, ck.psarc_iv);
  else memcpy(a->toc, raw, a->toc_len);
  free(raw);
  a->esize = be32(a->hdr + 16);
  a->nent = be32(a->hdr + 20);
  a->bsize = be32(a->hdr + 24);
  if (a->esize < 30 || (uint64_t)a->esize * a->nent > a->toc_len) return -1;
  int zb = a->bsize <= 0x10000 ? 2 : a->bsize <= 0x1000000 ? 3 : 4;
  uint32_t off = a->nent * a->esize;
  a->nz = (a->toc_len - off) / (uint32_t)zb;
  a->zs = malloc(sizeof *a->zs * (a->nz + 1));
  for (uint32_t i = 0; i < a->nz; i++) {
    uint32_t v = 0;
    for (int k = 0; k < zb; k++) v = v << 8 | a->toc[off + i * (uint32_t)zb + (uint32_t)k];
    a->zs[i] = v;
  }
  return 0;
}
static void cr_psarc_close(cr_psarc *a) {
  free(a->toc);
  free(a->zs);
}

// Entry i (0 = the manifest), inflated. Returns a malloc'd buffer or NULL.
static uint8_t *cr_psarc_entry(const char *path, const cr_psarc *a, uint32_t i, uint32_t *len) {
  if (i >= a->nent) return NULL;
  const uint8_t *e = a->toc + i * a->esize;
  uint32_t zi = be32(e + 16);
  uint64_t n = be40(e + 20), off = be40(e + 25);
  if (n > (256u << 20)) return NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t *out = malloc(n + 1), *blk = malloc(a->bsize + 16);
  uint64_t got = 0;
  fseeko(f, (off_t)off, SEEK_SET);
  while (got < n && zi < a->nz) {
    uint32_t z = a->zs[zi++];
    if (z == 0) z = a->bsize;
    if (fread(blk, 1, z, f) != z) break;
    uLongf want = n - got < a->bsize ? (uLongf)(n - got) : a->bsize;
    uLongf dl = want;
    if (z == want || uncompress(out + got, &dl, blk, z) != Z_OK) {
      memcpy(out + got, blk, z < want ? z : want);
      dl = z < want ? z : want;
    }
    got += dl;
  }
  fclose(f);
  free(blk);
  if (got != n) {
    free(out);
    return NULL;
  }
  *len = (uint32_t)n;
  return out;
}

// Finds the real-data inputs (deterministic: sorted names, fixed caps).
static void crypto_real_init(const char *game_dir) {
  cr_npsarcs = cr_nsngs = 0;
  cr_pkg.psarc = -1;
  if (!game_dir || !*game_dir) return;
  char dlc[1100];
  snprintf(dlc, sizeof dlc, "%s/dlc", game_dir);
  cr_add_dir(game_dir, 0);
  cr_add_dir(dlc, 1);
  int dlc_m_used = 0;
  for (int pi = 0; pi < cr_npsarcs; pi++) {
    const char *p = cr_psarcs[pi].path, *base = strrchr(p, '/') + 1;
    size_t bl = strlen(base);
    int is_songs = strcmp(base, "songs.psarc") == 0, is_static = strcmp(base, "static.psarc") == 0;
    int is_dlc_m = strstr(p, "/dlc/") && bl > 8 && strcmp(base + bl - 8, "_m.psarc") == 0;
    if (!is_songs && !is_static && !(is_dlc_m && dlc_m_used < 20)) continue;
    dlc_m_used += is_dlc_m;
    cr_psarc a;
    if (cr_psarc_open(p, &a)) continue;
    uint32_t ml;
    uint8_t *man = cr_psarc_entry(p, &a, 0, &ml);
    if (man) {
      man[ml] = 0;
      uint32_t idx = 1;
      for (char *s = (char *)man, *nl; s && *s; s = nl ? nl + 1 : NULL, idx++) {
        nl = strchr(s, '\n');
        if (nl) *nl = 0;
        size_t sl = strlen(s);
        if (is_static && strcmp(s, "pkgconfig.ini") == 0) cr_pkg = (cr_ref){pi, idx};
        if (!is_static && strstr(s, "/bin/macos/") && sl > 4 && strcmp(s + sl - 4, ".sng") == 0) {
          cr_sngs = realloc(cr_sngs, sizeof *cr_sngs * (size_t)(cr_nsngs + 1));
          cr_sngs[cr_nsngs++] = (cr_ref){pi, idx};
        }
      }
      free(man);
    }
    cr_psarc_close(&a);
  }
}

static uint8_t *cr_read_ref(cr_ref r, uint32_t *len) {
  cr_psarc a;
  if (r.psarc < 0 || cr_psarc_open(cr_psarcs[r.psarc].path, &a)) return NULL;
  uint8_t *b = cr_psarc_entry(cr_psarcs[r.psarc].path, &a, r.entry, len);
  cr_psarc_close(&a);
  return b;
}

// --- groups ------------------------------------------------------------------------
enum { CR_BPE, CR_RIJNDAEL, CR_CTR, CR_CFB, CR_DSA, CR_PSARC, CR_SNG, CR_PROFILE, CR_PKGCONFIG, CR_PREVIEW, CR_GROUPS };
static const char *const crypto_group_names[CR_GROUPS] = {"bpe", "rijndael", "ctr", "cfb", "dsa",
                                                          "psarc", "sng", "profile", "pkgconfig", "preview"};
#define CR_DSA_CASES_PER_KEY 120
static int crypto_group_cases(int g) {
  switch (g) {
    case CR_BPE: return 3 + 2000;
    case CR_RIJNDAEL: return 6 + 1000;
    case CR_CTR: return 500;
    case CR_CFB: return 500;
    case CR_DSA: return CR_DSA_KEYS * CR_DSA_CASES_PER_KEY + 40;
    case CR_PSARC: return cr_npsarcs;
    case CR_SNG: return cr_nsngs;
    case CR_PROFILE: return 500;
    case CR_PKGCONFIG: return 60 + (cr_pkg.psarc >= 0);
    case CR_PREVIEW: return 100;
  }
  return 0;
}

static void cr_bpe_case(const crypto_api *a, int i) {
  if (i < 3) {  // the game's blobs: only properties, never the key text
    const char *k = i == 0 ? ck.sng_dsa : i == 1 ? ck.sng_aes : ck.pkg_dsa;
    size_t n = strlen(k), hex = 0;
    for (size_t j = 0; j < n; j++) hex += (k[j] >= '0' && k[j] <= '9') || (k[j] >= 'A' && k[j] <= 'F') || (k[j] >= 'a' && k[j] <= 'f');
    efd_txf("blob %d: %d bytes, %zu chars, %s\n", i, ck.blob_len[i], n, hex == n ? "all hex" : "not hex");
    return;
  }
  static uint8_t in[200000], want[300000], out[300000];
  int pl, n = cr_bpe_gen(in, want, &pl);
  memset(out, 0xa5, (size_t)pl + 64);
  int r = a->bpe(in, n, out);
  efd_txf("in %d ret %d crc %08lx tail %02x expected %d\n", n, r, cr_crc(out, r > 0 ? (size_t)r : 0), out[pl],
          r == pl && memcmp(out, want, (size_t)pl) == 0);
}

static void cr_rijndael_case(const crypto_api *a, int i) {
  static const char *const fips_key = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  static const uint8_t fips_pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  uint8_t key[32], pt[16], ct[16], back[16];
  uint32_t rk[64], rkd[64];
  int bits;
  if (i < 3) {  // FIPS-197 appendix C
    rsg_hex_decode(fips_key, 64, key);
    bits = 128 + 64 * i;
    memcpy(pt, fips_pt, 16);
  } else if (i < 6) {  // unsupported sizes: Nr 0, partial schedule
    cr_bytes(key, 32);
    bits = (int[]){0, 64, 257}[i - 3];
    cr_bytes(pt, 16);
  } else {
    cr_bytes(key, 32);
    bits = 128 + 64 * (int)(cr_next() % 3);
    cr_bytes(pt, 16);
  }
  memset(rk, 0x5a, sizeof rk);
  memset(rkd, 0x5a, sizeof rkd);
  int nr = a->ks_enc(rk, key, bits), nrd = a->ks_dec(rkd, key, bits);
  efd_txf("bits %d nr %d/%d rk %08lx rkd %08lx\n", bits, nr, nrd, cr_crc(rk, sizeof rk), cr_crc(rkd, sizeof rkd));
  if (nr == 0) return;
  for (int b = 0; b < (i < 3 ? 1 : 8); b++) {
    if (b) cr_bytes(pt, 16);
    a->enc(rk, nr, pt, ct);
    a->dec(rkd, nrd, ct, back);
    if (i < 3) {
      efd_txf("ct %02x%02x%02x%02x...%02x%02x round trip %d\n", ct[0], ct[1], ct[2], ct[3], ct[14], ct[15],
              memcmp(back, pt, 16) == 0);
    } else {
      efd_txf("ct %08lx round trip %d\n", cr_crc(ct, 16), memcmp(back, pt, 16) == 0);
    }
  }
}

// A key for the AES wrappers: valid or not, in some spelling.
static uint32_t cr_aes_key(char *hex, size_t cap) {
  static const int lens[] = {32, 32, 32, 16, 24, 0, 5, 15, 17, 31, 33, 48};
  uint8_t k[48];
  int n = lens[cr_next() % (sizeof lens / sizeof *lens)];
  cr_bytes(k, (size_t)n);
  size_t hl = cr_hex(hex, k, (size_t)n, (int)(cr_next() % 4));
  (void)cap;
  uint32_t r = cr_next() % 8;
  return r == 0 ? (uint32_t)hl + 1 : r == 1 && hl > 2 ? (uint32_t)hl - 2 : (uint32_t)hl;  // with the NUL, or cut short
}

static void cr_ctr_case(const crypto_api *a, int i) {
  char hex[400];
  uint32_t hl = cr_aes_key(hex, sizeof hex);
  uint32_t r = cr_next() % 100;
  size_t len = 16 + (r < 3 ? (size_t)(cr_next() % (1u << 20)) : r < 10 ? cr_next() % 16 : cr_next() % 20000);
  if (i == 0) len = 16;
  uint32_t outlen = (uint32_t)(len - 16);
  r = cr_next() % 4;
  if (r == 0 && outlen) outlen = cr_next() % outlen;
  if (r == 1) outlen += cr_next() % 100;
  uint8_t *data = malloc(len), *out = malloc(len + 200);
  cr_bytes(data, len);
  memset(out, 0xcc, len + 200);
  bool ok = a->ctr(data, len, hex, hl, out, outlen);
  efd_txf("len %zu out %u key %u ret %d crc %08lx\n", len, outlen, hl, ok, cr_crc(out, len + 200));
  free(data);
  free(out);
}

static void cr_cfb_case(const crypto_api *a, int i) {
  char hex[400];
  uint32_t hl = cr_aes_key(hex, sizeof hex);
  uint32_t r = cr_next() % 100;
  uint32_t len = r < 3 ? cr_next() % (1u << 20) : r < 15 ? cr_next() % 33 : cr_next() % 20000;
  if (i == 0) len = 0;
  uint8_t *in = malloc(len + 200), *out = malloc(len + 200);
  cr_bytes(in, len);
  memset(out, 0xcc, len + 200);
  int inplace = cr_next() % 4 == 0;
  if (inplace) memcpy(out, in, len);
  bool ok = a->cfb(inplace ? out : in, len, hex, hl, out);
  efd_txf("len %u key %u inplace %d ret %d crc %08lx\n", len, hl, inplace, ok, cr_crc(out, len + 200));
  free(in);
  free(out);
}

static void cr_dsa_case(const crypto_api *a, int i) {
  static uint8_t msg[300000];
  char *hex = malloc(8000);
  if (i >= CR_DSA_KEYS * CR_DSA_CASES_PER_KEY) {  // the game's keys on data they did not sign
    size_t n = 60 + cr_next() % 3000;
    cr_bytes(msg, n);
    bool r1 = a->dsa(msg, (uint32_t)n, ck.sng_dsa), r2 = a->dsa(msg, (uint32_t)n, ck.pkg_dsa);
    efd_txf("game keys, %zu random bytes: %d %d\n", n, r1, r2);
    free(hex);
    return;
  }
  const cr_dsa_key *k = &cr_keys[i / CR_DSA_CASES_PER_KEY];
  int j = i % CR_DSA_CASES_PER_KEY;
  uint32_t r = cr_next() % 100;
  size_t n = r < 2 ? 100000 + cr_next() % 150000 : r < 10 ? cr_next() % 4 : cr_next() % 6000;
  cr_bytes(msg, n);
  size_t sl = cr_dsa_sign(k, msg, n), total = n + sl;
  size_t qb = sl / 2;
  // What to break.
  int what = j < 30 ? 0 : (int)(cr_next() % 12);
  int variant = KEY_PLAIN, style = (int)(cr_next() % 3);
  const char *desc = "valid";
  switch (what) {
    case 1: msg[cr_next() % (n ? n : 1)] ^= (uint8_t)(1u << (cr_next() % 8)); desc = n ? "message bit" : "r bit"; break;
    case 2: msg[n + cr_next() % qb] ^= (uint8_t)(1u << (cr_next() % 8)); desc = "r bit"; break;
    case 3: msg[n + qb + cr_next() % qb] ^= (uint8_t)(1u << (cr_next() % 8)); desc = "s bit"; break;
    case 4: memset(msg + n, 0, qb); desc = "r = 0"; break;
    case 5: rsg_bn_to_be(&k->q, msg + n + qb, qb); desc = "s = q"; break;
    case 6: total--; desc = "short by one"; break;
    case 7: msg[total++] = 0; desc = "one byte more"; break;
    case 8:  // not KEY_NO_PARAMS: Crypto++ then dereferences an empty group (the original crashes)
      variant = (int)(1 + cr_next() % (KEY_VARIANTS - 1));
      if (variant == KEY_NO_PARAMS) variant = KEY_PLAIN;
      desc = "key encoding";
      break;
    case 9: style = 3; desc = "odd hex digit"; break;
    case 10: k = &cr_keys[(i / CR_DSA_CASES_PER_KEY + 1) % CR_DSA_KEYS]; desc = "other key"; break;
    case 11: rsg_bn_to_be(&k->q, msg + n, qb); desc = "r = q"; break;
  }
  uint8_t der[1300];
  size_t dl = cr_dsa_der(k, variant, der);
  cr_hex(hex, der, dl, style);
  if (total < sl) total = sl;  // below the signature length the original reads out of bounds
  if (getenv("RS_CRYPTO_TRACE")) fprintf(stderr, "  %s variant %d style %d len %zu\n", desc, variant, style, total);
  bool ok = a->dsa(msg, (uint32_t)total, hex);
  efd_txf("key %d/%d %s variant %d style %d len %zu ret %d\n", k->L, k->N, desc, variant, style, total, ok);
  free(hex);
}

static void cr_psarc_case(const crypto_api *a, int i) {
  uint8_t hdr[32], *toc;
  uint32_t n;
  if (cr_psarc_toc(cr_psarcs[i].path, hdr, &toc, &n)) {
    efd_txf("unreadable\n");
    return;
  }
  uint8_t *out = malloc(n + 64);
  memset(out, 0xcc, n + 64);
  bool ok = a->cfb(toc, n, ck.psarc_hex, 0x41, out);  // like 0x100161780: header + 32, length - 32
  uint32_t nent = be32(hdr + 20), es = be32(hdr + 16);
  // A plausible TOC: the first entry's (manifest's) length and offset fit the archive's layout.
  uint64_t off0 = es >= 30 && n >= 30 ? be40(out + 25) : 0;
  efd_txf("toc %u bytes, %u entries, flags %u: ret %d crc %08lx entry0 offset %s\n", n, nent, be32(hdr + 28), ok,
          cr_crc(out, n + 64), off0 >= be32(hdr + 12) ? "after the toc" : "inside the toc");
  // A prefix, decrypted in place (a partial last block).
  uint32_t m = n ? cr_next() % n : 0;
  memcpy(out, toc, m);
  ok = a->cfb(out, m, ck.psarc_hex, 0x41, out);
  efd_txf("prefix %u in place: ret %d crc %08lx\n", m, ok, cr_crc(out, m));
  free(toc);
  free(out);
}

static void cr_sng_case(const crypto_api *a, int i) {
  uint32_t n;
  uint8_t *f = cr_read_ref(cr_sngs[i], &n);
  if (!f || n < 8 + 16 + 56) {
    efd_txf("unreadable\n");
    free(f);
    return;
  }
  uint32_t magic = (uint32_t)f[0] | (uint32_t)f[1] << 8 | (uint32_t)f[2] << 16 | (uint32_t)f[3] << 24;
  uint32_t flags = (uint32_t)f[4] | (uint32_t)f[5] << 8 | (uint32_t)f[6] << 16 | (uint32_t)f[7] << 24;
  int flip = cr_next() % 4 == 0;
  uint32_t at = 8 + cr_next() % (n - 8);
  if (flip) f[at] ^= (uint8_t)(1u << (cr_next() % 8));
  // 0x1001eb6a0: verify(data + 8, size - 8, key), then CTR into a buffer of size - 8.
  bool v = a->dsa(f + 8, n - 8, ck.sng_dsa);
  uint8_t *out = malloc(n);
  memset(out, 0xcc, n);
  bool ok = a->ctr(f + 8, n - 8, ck.sng_aes, 0x40, out, n - 8);
  uint32_t usize = (uint32_t)out[0] | (uint32_t)out[1] << 8 | (uint32_t)out[2] << 16 | (uint32_t)out[3] << 24;
  int zr = -99;
  if (usize < (64u << 20)) {
    uint8_t *z = malloc(usize + 1);
    uLongf zl = usize;
    zr = uncompress(z, &zl, out + 4, n - 8 - 16 - 4);
    if (zr == Z_OK && zl != usize) zr = -98;
    free(z);
  }
  efd_txf("sng magic %x flags %u %u bytes%s: verify %d ctr %d crc %08lx zlib %d\n", magic, flags, n,
          flip ? " (one bit flipped)" : "", v, ok, cr_crc(out, n), zr);
  free(out);
  free(f);
}

static void cr_profile_case(const crypto_api *a, int i) {
  uint32_t r = cr_next() % 100;
  size_t pl = i < 8 ? (size_t)(int[]){0, 1, 15, 16, 17, 31, 32, 33}[i] : r < 5 ? 100000 + cr_next() % 200000 : cr_next() % 20000;
  uint8_t *plain = malloc(pl + 1), *z = malloc(pl + pl / 65535 * 5 + 64), *file = calloc(1, pl + pl / 65535 * 5 + 200);
  static const char words[] = "{\"PlayerName\":\"Guitarist\",\"Stats\":[1,2,3],\"Songs\":{\"x\":0.5}}  ";
  for (size_t k = 0; k < pl; k++) plain[k] = cr_next() % 8 ? (uint8_t)words[cr_next() % (sizeof words - 1)] : (uint8_t)cr_next();
  size_t zl = i < 8 ? pl : cr_zlib_stored(plain, pl, z);  // the first cases encrypt raw lengths
  if (i < 8) memcpy(z, plain, pl);
  // The save function: AES in place, rand() padding.
  unsigned seed = cr_next();
  int len = (int)zl;
  memcpy(file + 20, z, zl);
  srand(seed);
  a->profile_encrypt(file + 20, &len);
  efd_txf("plain %zu z %zu -> %d crc %08lx\n", pl, zl, len, cr_crc(file + 20, (size_t)len));
  if (i < 8) goto out;
  {
    memcpy(file, "EVAS", 4);
    uint32_t ver = 1, us = (uint32_t)pl;
    memcpy(file + 4, &ver, 4);
    cr_bytes(file + 8, 8);
    memcpy(file + 16, &us, 4);
    uint32_t size = 20 + (uint32_t)len;
    const char *dmg = "intact";
    switch (cr_next() % 16) {
      case 0: file[1] ^= 0x20; dmg = "magic"; break;
      case 1: memset(file + 4, 0, 4); dmg = "version 0"; break;
      case 2: file[4] = 2; dmg = "version 2"; break;
      case 3: size = 16 + cr_next() % 4; dmg = "16..19 bytes"; break;
      case 4: size = 1 + cr_next() % 15; dmg = "short"; break;  // 0: fread's error (code 11), game I/O
      case 5: size += 1 + cr_next() % 15; dmg = "body not whole blocks"; break;
      case 6: file[20 + cr_next() % (uint32_t)len] ^= (uint8_t)(1u << (cr_next() % 8)); dmg = "body bit"; break;
      case 7: us += 1; memcpy(file + 16, &us, 4); dmg = "size + 1"; break;
      case 8: us = us ? us - 1 : 5; memcpy(file + 16, &us, 4); dmg = "size - 1"; break;
      case 9: size = 20; dmg = "empty body"; break;
    }
    int fail = -1, code = -1;
    uint8_t *data = NULL;
    uint32_t dl = 0;
    a->profile_load(file, size, &fail, &code, &data, &dl);
    efd_txf("load (%s, %u bytes): fail %d code %d len %u crc %08lx match %d\n", dmg, size, fail, code, dl,
            data ? cr_crc(data, dl) : 0ul, data && dl == pl && memcmp(data, plain, pl) == 0);
    free(data);
  }
out:
  free(plain);
  free(z);
  free(file);
}

static void cr_pkgconfig_case(const crypto_api *a, int i) {
  uint8_t *file = NULL;
  uint32_t n = 0;
  const char *what;
  if (i == 60) {
    file = cr_read_ref(cr_pkg, &n);
    what = "static.psarc pkgconfig.ini";
    if (!file) {
      efd_txf("unreadable\n");
      return;
    }
  } else {
    // A generated config: ini text, NUL-padded, AES-256-ECB, then 0 or 0x38 junk bytes
    // (it never verifies: the signature would need the game's private key).
    size_t tl = 1 + cr_next() % 3000;
    file = calloc(1, tl + 200);
    static const char ini[] = "[Global]\nVersion=1\nDLCKey=12345\nName=Song Pack\n";
    for (size_t k = 0; k < tl; k++) file[k] = (uint8_t)ini[cr_next() % (sizeof ini - 1)];
    if (cr_next() % 5 == 0) file[cr_next() % tl] = 0;  // an early NUL
    size_t padded = (tl + 1 + 15) & ~(size_t)15;
    memset(file + tl, 0, padded - tl);
    rsg_aes256_ecb_encrypt_blocks(ck.pkg_aes, file, padded);
    n = (uint32_t)padded;
    switch (cr_next() % 4) {
      case 0: what = "no signature"; break;
      case 1: cr_bytes(file + n, 0x38); n += 0x38; what = "junk signature"; break;
      case 2: cr_bytes(file + n, 7); n += 7; what = "partial last block"; break;
      default: file[cr_next() % n] ^= 1; what = "damaged"; break;
    }
  }
  uint8_t *out = NULL;
  uint32_t t = a->pkgconfig(file, n, &out);
  efd_txf("%s, %u bytes: text %u crc %08lx\n", what, n, t, cr_crc(out, t));
  free(out);
  free(file);
}

static void cr_preview_case(const crypto_api *a, int i) {
  size_t n = (size_t)i * 7 % 300;
  uint8_t *b = malloc(n + 16);
  cr_bytes(b, n + 16);
  a->preview_ecb(b, n);
  efd_txf("%zu bytes: crc %08lx\n", n, cr_crc(b, n + 16));
  free(b);
}

static void crypto_run_case(const crypto_api *a, int g, int i) {
  efd_tx_reset();
  cr_seed(g, i);
  switch (g) {
    case CR_BPE: cr_bpe_case(a, i); break;
    case CR_RIJNDAEL: cr_rijndael_case(a, i); break;
    case CR_CTR: cr_ctr_case(a, i); break;
    case CR_CFB: cr_cfb_case(a, i); break;
    case CR_DSA: cr_dsa_case(a, i); break;
    case CR_PSARC: cr_psarc_case(a, i); break;
    case CR_SNG: cr_sng_case(a, i); break;
    case CR_PROFILE: cr_profile_case(a, i); break;
    case CR_PKGCONFIG: cr_pkgconfig_case(a, i); break;
    case CR_PREVIEW: cr_preview_case(a, i); break;
  }
}

#endif
