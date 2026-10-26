// The crypto rewrites (include/rs_crypto.h) behind crypto_api, with the keys
// the originals read from their own image passed in from crypto_keys.
#ifndef RS_GAME_CRYPTO_REWRITE_H
#define RS_GAME_CRYPTO_REWRITE_H

#include <stdlib.h>

#include "crypto_cases.h"
#include "rs_crypto.h"

static bool rw_cfb(const uint8_t *in, uint32_t len, const char *hex, uint32_t hl, uint8_t *out) {
  return rsg_cryptopp_aes_cfb_decrypt(in, len, hex, hl, out, ck.psarc_iv);
}
static int rw_rand(void) { return rand(); }
static void rw_profile_encrypt(uint8_t *buf, int *len) { rsg_profile_encrypt(buf, len, ck.profile, rw_rand); }
static void rw_profile_load(const uint8_t *file, uint32_t n, int *fail, int *code, uint8_t **out, uint32_t *len) {
  uint8_t *copy = malloc(n ? n : 1);
  memcpy(copy, file, n);
  *code = rsg_profile_decode(copy, n, ck.profile, false, 0, out, len);
  *fail = *code != 0;
  free(copy);
}
static uint32_t rw_pkgconfig(const uint8_t *file, uint32_t n, uint8_t **out) {
  uint8_t *buf = malloc(n ? n : 1);
  memcpy(buf, file, n);
  bool verified;
  uint32_t t = rsg_pkgconfig_decrypt(buf, n, ck.pkg_dsa, ck.pkg_aes, &verified);
  // What the game logs (0x1002a7550: module 0x10, the level byte at 0x10142cdf8).
  if (!verified) efd_log_line(16, 0, "MainService.cpp", 0x168, "IniDecyptFunc failed to Verify pkgconfig file.");
  *out = buf;
  return t;
}
static void rw_ecb_decrypt(uint8_t *buf, size_t n) { rsg_aes256_ecb_decrypt_blocks(ck.preview, buf, n); }

static const crypto_api crypto_rewrite_api = {
    rsg_rijndael_key_setup_enc,
    rsg_rijndael_key_setup_dec,
    rsg_rijndael_encrypt,
    rsg_rijndael_decrypt,
    rsg_bpe_expand,
    rsg_cryptopp_aes_ctr_decrypt,
    rw_cfb,
    rsg_cryptopp_dsa_sha1_verify,
    rw_profile_encrypt,
    rw_profile_load,
    rw_pkgconfig,
    rw_ecb_decrypt,
};

#endif
