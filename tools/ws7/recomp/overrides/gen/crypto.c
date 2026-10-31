// Adapters of override module `crypto` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_crypto.h"

void RC_FN(rsg_rijndael_key_setup_enc)(rc_ctx *c) {
  OV_ARGS_OF(rsg_rijndael_key_setup_enc, a, c);
  uint32_t * p0 = (uint32_t *)(uintptr_t)OV_U64(a);
  const uint8_t * p1 = (const uint8_t *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int r = rsg_rijndael_key_setup_enc(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_rijndael_key_setup_dec)(rc_ctx *c) {
  OV_ARGS_OF(rsg_rijndael_key_setup_dec, a, c);
  uint32_t * p0 = (uint32_t *)(uintptr_t)OV_U64(a);
  const uint8_t * p1 = (const uint8_t *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int r = rsg_rijndael_key_setup_dec(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_rijndael_encrypt)(rc_ctx *c) {
  OV_ARGS_OF(rsg_rijndael_encrypt, a, c);
  const uint32_t * p0 = (const uint32_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const uint8_t * p2 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint8_t * p3 = (uint8_t *)(uintptr_t)OV_U64(a);
  rsg_rijndael_encrypt(p0, p1, p2, p3);
  ov_ret_void(c);
}

void RC_FN(rsg_rijndael_decrypt)(rc_ctx *c) {
  OV_ARGS_OF(rsg_rijndael_decrypt, a, c);
  const uint32_t * p0 = (const uint32_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const uint8_t * p2 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint8_t * p3 = (uint8_t *)(uintptr_t)OV_U64(a);
  rsg_rijndael_decrypt(p0, p1, p2, p3);
  ov_ret_void(c);
}

void RC_FN(rsg_bpe_expand)(rc_ctx *c) {
  OV_ARGS_OF(rsg_bpe_expand, a, c);
  const uint8_t * p0 = (const uint8_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  uint8_t * p2 = (uint8_t *)(uintptr_t)OV_U64(a);
  int r = rsg_bpe_expand(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_cryptopp_aes_ctr_decrypt)(rc_ctx *c) {
  OV_ARGS_OF(rsg_cryptopp_aes_ctr_decrypt, a, c);
  const uint8_t * p0 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint64_t p1 = (uint64_t)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  uint32_t p3 = (uint32_t)OV_U64(a);
  uint8_t * p4 = (uint8_t *)(uintptr_t)OV_U64(a);
  uint32_t p5 = (uint32_t)OV_U64(a);
  _Bool r = rsg_cryptopp_aes_ctr_decrypt(p0, p1, p2, p3, p4, p5);
  OV_RET_I32(c, r);
}

void RC_FN(rsg_cryptopp_dsa_sha1_verify)(rc_ctx *c) {
  OV_ARGS_OF(rsg_cryptopp_dsa_sha1_verify, a, c);
  const uint8_t * p0 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  _Bool r = rsg_cryptopp_dsa_sha1_verify(p0, p1, p2);
  OV_RET_I32(c, r);
}
