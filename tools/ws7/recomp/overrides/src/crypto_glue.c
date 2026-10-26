// Crypto adapters that are not a plain translation.
#include "rc_ov.h"
#include "rs_crypto.h"

/* 0x101376f8f Crypto++ AES-CFB decrypt (PSARC table of contents): (in, len, key_hex, key_hex_len, out).
 * The original takes the IV from its own image (RS_KEY_PSARC_IV, 16 bytes at 0x10142e3d0); the
 * rewrite wants it as an argument, and the image is mapped, so it is read from there. */
void F_101376f8f(rc_ctx *c) {
  OV_ARGS(a, c);
  const uint8_t *in = OV_PTR(a, const uint8_t);
  uint32_t len = OV_U32(a);
  const char *hex = OV_PTR(a, const char);
  uint32_t hexlen = OV_U32(a);
  uint8_t *out = OV_PTR(a, uint8_t);
  bool r = rsg_cryptopp_aes_cfb_decrypt(in, len, hex, hexlen, out, (const uint8_t *)(uintptr_t)RC_A(RS_KEY_PSARC_IV));
  OV_RET_I32(c, r);
}
