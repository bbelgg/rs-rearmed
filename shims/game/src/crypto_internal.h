// Internals of the crypto rewrites (rs_crypto.h), shared with the tests'
// DSA signer (test/crypto_cases.h). Not part of the game-facing API.
#ifndef RS_CRYPTO_INTERNAL_H
#define RS_CRYPTO_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Crypto++ HexDecoder: returns the bytes written (at most n / 2).
size_t rsg_hex_decode(const char *hex, size_t n, uint8_t *out);

// Unsigned bignums, little-endian 64-bit limbs, up to RSG_BN_LIMBS limbs.
// n is the number of significant limbs (0 for zero).
#define RSG_BN_LIMBS 72 /* 4,608 bits: products of two 2,304-bit values */
typedef struct {
  int n;
  uint64_t d[RSG_BN_LIMBS];
} rsg_bn;

bool rsg_bn_from_be(rsg_bn *a, const uint8_t *p, size_t len);  // false if too large
void rsg_bn_to_be(const rsg_bn *a, uint8_t *p, size_t len);    // zero-padded, truncated at the top
void rsg_bn_set_u64(rsg_bn *a, uint64_t v);
int rsg_bn_cmp(const rsg_bn *a, const rsg_bn *b);
int rsg_bn_bits(const rsg_bn *a);
int rsg_bn_bytes(const rsg_bn *a);
bool rsg_bn_is_odd(const rsg_bn *a);
void rsg_bn_add(rsg_bn *r, const rsg_bn *a, const rsg_bn *b);
void rsg_bn_sub(rsg_bn *r, const rsg_bn *a, const rsg_bn *b);  // a >= b
void rsg_bn_shr(rsg_bn *r, const rsg_bn *a, int bits);
bool rsg_bn_mul(rsg_bn *r, const rsg_bn *a, const rsg_bn *b);  // false on overflow
// r = a mod m (m != 0); q (optional) = a / m.
void rsg_bn_divmod(rsg_bn *q, rsg_bn *r, const rsg_bn *a, const rsg_bn *m);
bool rsg_bn_modmul(rsg_bn *r, const rsg_bn *a, const rsg_bn *b, const rsg_bn *m);
// r = b^e mod m for odd m (Montgomery). False if m is even or too large.
bool rsg_bn_modexp(rsg_bn *r, const rsg_bn *b, const rsg_bn *e, const rsg_bn *m);
// r = a^-1 mod m, or 0 if gcd(a, m) != 1 (Crypto++'s Integer::InverseMod).
void rsg_bn_modinv(rsg_bn *r, const rsg_bn *a, const rsg_bn *m);

#ifdef __cplusplus
}
#endif

#endif
