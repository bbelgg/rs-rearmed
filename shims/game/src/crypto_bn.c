// Small unsigned bignum for the DSA verify (crypto_internal.h). Only what a
// 2048/224 DSA check needs: Montgomery exponentiation for odd moduli, and
// bit-serial division for the few reductions around it.
#include <string.h>

#include "crypto_internal.h"

typedef unsigned __int128 u128;

static void norm(rsg_bn *a) {
  while (a->n > 0 && a->d[a->n - 1] == 0) a->n--;
}

bool rsg_bn_from_be(rsg_bn *a, const uint8_t *p, size_t len) {
  while (len && *p == 0) p++, len--;
  if (len > RSG_BN_LIMBS * 8) return false;
  memset(a, 0, sizeof *a);
  for (size_t i = 0; i < len; i++) {
    size_t bit = (len - 1 - i) * 8;
    a->d[bit / 64] |= (uint64_t)p[i] << (bit % 64);
  }
  a->n = (int)((len + 7) / 8);
  norm(a);
  return true;
}

void rsg_bn_to_be(const rsg_bn *a, uint8_t *p, size_t len) {
  for (size_t i = 0; i < len; i++) {
    size_t bit = (len - 1 - i) * 8;
    p[i] = bit / 64 < (size_t)a->n ? (uint8_t)(a->d[bit / 64] >> (bit % 64)) : 0;
  }
}

void rsg_bn_set_u64(rsg_bn *a, uint64_t v) {
  memset(a, 0, sizeof *a);
  a->d[0] = v;
  a->n = v != 0;
}

int rsg_bn_cmp(const rsg_bn *a, const rsg_bn *b) {
  if (a->n != b->n) return a->n < b->n ? -1 : 1;
  for (int i = a->n - 1; i >= 0; i--)
    if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
  return 0;
}

int rsg_bn_bits(const rsg_bn *a) { return a->n ? 64 * (a->n - 1) + 64 - __builtin_clzll(a->d[a->n - 1]) : 0; }
int rsg_bn_bytes(const rsg_bn *a) { return (rsg_bn_bits(a) + 7) / 8; }
bool rsg_bn_is_odd(const rsg_bn *a) { return a->n && (a->d[0] & 1); }

void rsg_bn_add(rsg_bn *r, const rsg_bn *a, const rsg_bn *b) {
  int n = a->n > b->n ? a->n : b->n;
  uint64_t c = 0;
  rsg_bn t;
  memset(&t, 0, sizeof t);
  for (int i = 0; i < n; i++) {
    u128 s = (u128)(i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0) + c;
    t.d[i] = (uint64_t)s;
    c = (uint64_t)(s >> 64);
  }
  if (n < RSG_BN_LIMBS) t.d[n++] = c;
  t.n = n;
  norm(&t);
  *r = t;
}

void rsg_bn_sub(rsg_bn *r, const rsg_bn *a, const rsg_bn *b) {
  uint64_t borrow = 0;
  rsg_bn t;
  memset(&t, 0, sizeof t);
  for (int i = 0; i < a->n; i++) {
    uint64_t x = a->d[i], y = i < b->n ? b->d[i] : 0;
    uint64_t d = x - y - borrow;
    borrow = (x < y) || (x == y && borrow) ? 1 : 0;
    t.d[i] = d;
  }
  t.n = a->n;
  norm(&t);
  *r = t;
}

void rsg_bn_shr(rsg_bn *r, const rsg_bn *a, int bits) {
  rsg_bn t;
  memset(&t, 0, sizeof t);
  int w = bits / 64, s = bits % 64;
  for (int i = 0; i + w < a->n; i++) {
    uint64_t lo = a->d[i + w] >> s;
    uint64_t hi = s && i + w + 1 < a->n ? a->d[i + w + 1] << (64 - s) : 0;
    t.d[i] = lo | hi;
  }
  t.n = a->n - w > 0 ? a->n - w : 0;
  norm(&t);
  *r = t;
}

bool rsg_bn_mul(rsg_bn *r, const rsg_bn *a, const rsg_bn *b) {
  if (a->n + b->n > RSG_BN_LIMBS) return false;
  rsg_bn t;
  memset(&t, 0, sizeof t);
  for (int i = 0; i < a->n; i++) {
    uint64_t c = 0;
    for (int j = 0; j < b->n; j++) {
      u128 p = (u128)a->d[i] * b->d[j] + t.d[i + j] + c;
      t.d[i + j] = (uint64_t)p;
      c = (uint64_t)(p >> 64);
    }
    t.d[i + b->n] = c;
  }
  t.n = a->n + b->n;
  norm(&t);
  *r = t;
  return true;
}

// r = 2r + bit, in place; r < 2^(64*(RSG_BN_LIMBS-1)) assumed.
static void shl1_add(rsg_bn *r, int bit) {
  uint64_t c = (uint64_t)bit;
  for (int i = 0; i < r->n; i++) {
    uint64_t x = r->d[i];
    r->d[i] = x << 1 | c;
    c = x >> 63;
  }
  if (c) r->d[r->n++] = c;
  else if (r->n == 0 && bit) r->n = 1;
}

void rsg_bn_divmod(rsg_bn *q, rsg_bn *r, const rsg_bn *a, const rsg_bn *m) {
  rsg_bn rem, quo;
  memset(&rem, 0, sizeof rem);
  memset(&quo, 0, sizeof quo);
  for (int i = rsg_bn_bits(a) - 1; i >= 0; i--) {
    shl1_add(&rem, (int)(a->d[i / 64] >> (i % 64) & 1));
    if (rsg_bn_cmp(&rem, m) >= 0) {
      rsg_bn_sub(&rem, &rem, m);
      quo.d[i / 64] |= 1ull << (i % 64);
    }
  }
  quo.n = a->n;
  norm(&quo);
  if (q) *q = quo;
  if (r) *r = rem;
}

bool rsg_bn_modmul(rsg_bn *r, const rsg_bn *a, const rsg_bn *b, const rsg_bn *m) {
  rsg_bn t;
  if (!rsg_bn_mul(&t, a, b)) return false;
  rsg_bn_divmod(NULL, r, &t, m);
  return true;
}

// --- Montgomery ----------------------------------------------------------------
typedef struct {
  int n;
  const uint64_t *m;
  uint64_t minv;  // -m^-1 mod 2^64
} mont;

// r = a * b * 2^(-64n) mod m, a, b < m (n limbs each, zero-extended).
static void mont_mul(const mont *M, uint64_t *r, const uint64_t *a, const uint64_t *b) {
  int n = M->n;
  uint64_t t[RSG_BN_LIMBS + 2];
  memset(t, 0, (size_t)(n + 2) * 8);
  for (int i = 0; i < n; i++) {
    uint64_t c = 0;
    for (int j = 0; j < n; j++) {
      u128 p = (u128)a[j] * b[i] + t[j] + c;
      t[j] = (uint64_t)p;
      c = (uint64_t)(p >> 64);
    }
    u128 s = (u128)t[n] + c;
    t[n] = (uint64_t)s;
    t[n + 1] = (uint64_t)(s >> 64);
    uint64_t mm = t[0] * M->minv;
    u128 p = (u128)mm * M->m[0] + t[0];
    c = (uint64_t)(p >> 64);
    for (int j = 1; j < n; j++) {
      p = (u128)mm * M->m[j] + t[j] + c;
      t[j - 1] = (uint64_t)p;
      c = (uint64_t)(p >> 64);
    }
    s = (u128)t[n] + c;
    t[n - 1] = (uint64_t)s;
    t[n] = t[n + 1] + (uint64_t)(s >> 64);
  }
  // t < 2m: subtract m once if t >= m.
  int ge = t[n] != 0;
  if (!ge) {
    ge = 1;
    for (int i = n - 1; i >= 0; i--)
      if (t[i] != M->m[i]) {
        ge = t[i] > M->m[i];
        break;
      }
  }
  if (ge) {
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
      uint64_t x = t[i], y = M->m[i];
      t[i] = x - y - borrow;
      borrow = (x < y) || (x == y && borrow);
    }
  }
  memcpy(r, t, (size_t)n * 8);
}

// x = 2x mod m (x < m), n limbs.
static void dbl_mod(const mont *M, uint64_t *x) {
  int n = M->n;
  uint64_t c = 0;
  for (int i = 0; i < n; i++) {
    uint64_t v = x[i];
    x[i] = v << 1 | c;
    c = v >> 63;
  }
  int ge = c != 0;
  if (!ge) {
    ge = 1;
    for (int i = n - 1; i >= 0; i--)
      if (x[i] != M->m[i]) {
        ge = x[i] > M->m[i];
        break;
      }
  }
  if (ge) {
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
      uint64_t a = x[i], b = M->m[i];
      x[i] = a - b - borrow;
      borrow = (a < b) || (a == b && borrow);
    }
  }
}

bool rsg_bn_modexp(rsg_bn *r, const rsg_bn *b, const rsg_bn *e, const rsg_bn *m) {
  if (!rsg_bn_is_odd(m) || m->n > RSG_BN_LIMBS / 2) return false;
  int n = m->n;
  if (n == 1 && m->d[0] == 1) {
    rsg_bn_set_u64(r, 0);
    return true;
  }
  mont M = {n, m->d, 0};
  uint64_t inv = 1;
  for (int i = 0; i < 6; i++) inv *= 2 - m->d[0] * inv;
  M.minv = (uint64_t)0 - inv;

  uint64_t one_m[RSG_BN_LIMBS], r2[RSG_BN_LIMBS], base[RSG_BN_LIMBS], x[RSG_BN_LIMBS], one[RSG_BN_LIMBS];
  memset(one_m, 0, sizeof one_m);
  one_m[0] = 1;
  for (int i = 0; i < 64 * n; i++) dbl_mod(&M, one_m);  // R mod m
  memcpy(r2, one_m, sizeof r2);
  for (int i = 0; i < 64 * n; i++) dbl_mod(&M, r2);  // R^2 mod m

  rsg_bn bm;
  rsg_bn_divmod(NULL, &bm, b, m);
  memset(base, 0, sizeof base);
  memcpy(base, bm.d, (size_t)n * 8);
  mont_mul(&M, base, base, r2);  // to Montgomery form
  memcpy(x, one_m, sizeof x);
  for (int i = rsg_bn_bits(e) - 1; i >= 0; i--) {
    mont_mul(&M, x, x, x);
    if (e->d[i / 64] >> (i % 64) & 1) mont_mul(&M, x, x, base);
  }
  memset(one, 0, sizeof one);
  one[0] = 1;
  mont_mul(&M, x, x, one);
  memset(r, 0, sizeof *r);
  memcpy(r->d, x, (size_t)n * 8);
  r->n = n;
  norm(r);
  return true;
}

void rsg_bn_modinv(rsg_bn *r, const rsg_bn *a, const rsg_bn *m) {
  rsg_bn old_r = *m, rr, old_t, t, quo, rem, qt;
  if (m->n == 0) {
    rsg_bn_set_u64(r, 0);
    return;
  }
  rsg_bn_divmod(NULL, &rr, a, m);
  rsg_bn_set_u64(&old_t, 0);
  rsg_bn_set_u64(&t, 1);
  while (rr.n) {
    rsg_bn_divmod(&quo, &rem, &old_r, &rr);
    old_r = rr;
    rr = rem;
    rsg_bn_modmul(&qt, &quo, &t, m);
    rsg_bn nt;
    if (rsg_bn_cmp(&old_t, &qt) >= 0) {
      rsg_bn_sub(&nt, &old_t, &qt);
    } else {
      rsg_bn_add(&nt, &old_t, m);
      rsg_bn_sub(&nt, &nt, &qt);
    }
    old_t = t;
    t = nt;
  }
  if (!(old_r.n == 1 && old_r.d[0] == 1)) {
    rsg_bn_set_u64(r, 0);
    return;
  }
  rsg_bn_divmod(NULL, r, &old_t, m);
}
