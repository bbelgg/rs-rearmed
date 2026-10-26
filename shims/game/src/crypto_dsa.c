// Crypto++ DSA2<SHA1> verification as the game's wrapper 0x10137683d uses it
// (rs_crypto.h): StringSource(keyHex, HexDecoder) -> DSA::PublicKey::Load
// (X.509 SubjectPublicKeyInfo, BER) -> SignatureVerificationFilter with
// PUT_RESULT (signature at the end) over StringSource(data, len). Exceptions
// (BERDecodeErr from a bad key) are caught by the wrapper and give false.
#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_internal.h"
#include "rs_crypto.h"

// --- BER, the subset Crypto++'s decoders accept here ---------------------------
typedef struct {
  const uint8_t *p;
  size_t n;  // bytes left in this element's scope
} ber;

// Reads one definite-length element header with tag `tag`; *body is its
// content. Crypto++ throws on an indefinite length for the primitives and,
// in practice, on any length running past the data.
static bool ber_take(ber *b, uint8_t tag, ber *body) {
  if (b->n < 2 || b->p[0] != tag) return false;
  size_t i = 1, len = b->p[i++];
  if (len & 0x80) {
    size_t k = len & 0x7f;
    if (k == 0) return false;  // indefinite: not produced by any key we accept
    len = 0;
    while (k--) {
      if (i >= b->n || len >> (8 * (sizeof len - 1))) return false;
      len = len << 8 | b->p[i++];
    }
  }
  if (len > b->n - i) return false;
  body->p = b->p + i;
  body->n = len;
  b->p += i + len;
  b->n -= i + len;
  return true;
}

// INTEGER, decoded SIGNED like Integer::BERDecode; negative values are refused.
static bool ber_int(ber *b, rsg_bn *v) {
  ber c;
  if (!ber_take(b, 0x02, &c)) return false;
  if (c.n && (c.p[0] & 0x80)) return false;
  return rsg_bn_from_be(v, c.p, c.n);
}

static const uint8_t k_dsa_oid[] = {0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x01};  // 1.2.840.10040.4.1

typedef struct {
  rsg_bn p, q, g, y;
} dsa_key;

// X509PublicKey::BERDecode with DL_GroupParameters_IntegerBased parameters.
static bool parse_key(const uint8_t *der, size_t n, dsa_key *k) {
  ber all = {der, n}, spki, alg, oid, params, bits;
  if (!ber_take(&all, 0x30, &spki)) return false;  // trailing bytes are ignored
  if (!ber_take(&spki, 0x30, &alg)) return false;
  if (!ber_take(&alg, 0x06, &oid) || oid.n != sizeof k_dsa_oid || memcmp(oid.p, k_dsa_oid, oid.n))
    return false;
  if (alg.n == 0) return false;  // no parameters: the group stays empty, nothing verifies
  if (!ber_take(&alg, 0x30, &params) || alg.n) return false;
  if (!ber_int(&params, &k->p) || !ber_int(&params, &k->q)) return false;
  if (params.n == 0) {  // {p, g} only: g = q, q = (p - 1) / 2
    k->g = k->q;
    rsg_bn one;
    rsg_bn_set_u64(&one, 1);
    if (k->p.n == 0) return false;
    rsg_bn_sub(&k->q, &k->p, &one);
    rsg_bn_shr(&k->q, &k->q, 1);
  } else if (!ber_int(&params, &k->g) || params.n) {
    return false;
  }
  if (!ber_take(&spki, 0x03, &bits) || bits.n < 1 || bits.p[0] != 0) return false;
  bits.p++, bits.n--;
  if (!ber_int(&bits, &k->y) || bits.n || spki.n) return false;
  return true;
}

bool rsg_cryptopp_dsa_sha1_verify(const uint8_t *data, uint32_t len, const char *key_hex) {
  size_t hn = strlen(key_hex);
  uint8_t *der = malloc(hn / 2 + 1);
  size_t dn = rsg_hex_decode(key_hex, hn, der);
  dsa_key k;
  bool ok = parse_key(der, dn, &k);
  free(der);
  if (!ok || k.q.n == 0) return false;

  size_t qb = (size_t)rsg_bn_bytes(&k.q), sig = 2 * qb;
  if (len < sig) return false;
  size_t mlen = len - sig;

  // DL_SignatureMessageEncodingMethod_DSA: the digest, left-padded or
  // truncated to bytes(q) and shifted down to bits(q).
  uint8_t h[CC_SHA1_DIGEST_LENGTH], rep[RSG_BN_LIMBS * 8];
  CC_SHA1_CTX ctx;
  CC_SHA1_Init(&ctx);
  for (size_t off = 0; off < mlen;) {  // CC_LONG is 32 bits
    size_t c = mlen - off > 0x40000000 ? 0x40000000 : mlen - off;
    CC_SHA1_Update(&ctx, data + off, (CC_LONG)c);
    off += c;
  }
  CC_SHA1_Final(h, &ctx);
  int qbits = rsg_bn_bits(&k.q);
  size_t replen = (size_t)(qbits + 7) / 8, pad = replen > sizeof h ? replen - sizeof h : 0;
  if (replen > sizeof rep) return false;
  memset(rep, 0, pad);
  memcpy(rep + pad, h, replen - pad < sizeof h ? replen - pad : sizeof h);
  rsg_bn e, r, s, w, u1, u2, v1, v2, v;
  rsg_bn_from_be(&e, rep, replen);
  if (sizeof h * 8 > (size_t)qbits) rsg_bn_shr(&e, &e, (int)(replen * 8 - (size_t)qbits));

  rsg_bn_from_be(&r, data + mlen, qb);
  rsg_bn_from_be(&s, data + mlen + qb, qb);
  // DL_Algorithm_GDSA::Verify
  if (r.n == 0 || s.n == 0 || rsg_bn_cmp(&r, &k.q) >= 0 || rsg_bn_cmp(&s, &k.q) >= 0) return false;
  rsg_bn_modinv(&w, &s, &k.q);
  if (!rsg_bn_modmul(&u1, &e, &w, &k.q) || !rsg_bn_modmul(&u2, &r, &w, &k.q)) return false;
  if (!rsg_bn_modexp(&v1, &k.g, &u1, &k.p) || !rsg_bn_modexp(&v2, &k.y, &u2, &k.p)) return false;
  if (!rsg_bn_modmul(&v, &v1, &v2, &k.p)) return false;
  rsg_bn_divmod(NULL, &v, &v, &k.q);
  return rsg_bn_cmp(&v, &r) == 0;
}
