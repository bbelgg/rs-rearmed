// Crypto adapters that are not a plain translation.
#include "rc_ov.h"
#include "rs_crypto.h"

/* Crypto++ AES-CFB decrypt (PSARC table of contents): (in, len, key_hex, key_hex_len, out).
 * The original takes the IV from its own image (psarc_key_iv, 16 bytes); the
 * rewrite wants it as an argument, and the image is mapped, so it is read from there. */
void RC_FN(rsg_cryptopp_aes_cfb_decrypt)(rc_ctx *c) {
  OV_ARGS_OF(rsg_cryptopp_aes_cfb_decrypt, a, c);
  const uint8_t *in = OV_PTR(a, const uint8_t);
  uint32_t len = OV_U32(a);
  const char *hex = OV_PTR(a, const char);
  uint32_t hexlen = OV_U32(a);
  uint8_t *out = OV_PTR(a, uint8_t);
  bool r = rsg_cryptopp_aes_cfb_decrypt(in, len, hex, hexlen, out, (const uint8_t *)(uintptr_t)RC_GA(psarc_key_iv));
  OV_RET_I32(c, r);
}
