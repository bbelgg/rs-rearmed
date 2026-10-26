// Adapters of override module `crypto` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "rs_crypto.h"

/* 0x100001b40 rsg_rijndael_key_setup_enc */
void F_100001b40(rc_ctx *c) {
  OV_ARGS(a, c);
  uint32_t * p0 = (uint32_t *)(uintptr_t)OV_U64(a);
  const uint8_t * p1 = (const uint8_t *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int r = rsg_rijndael_key_setup_enc(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100001eb0 rsg_rijndael_key_setup_dec */
void F_100001eb0(rc_ctx *c) {
  OV_ARGS(a, c);
  uint32_t * p0 = (uint32_t *)(uintptr_t)OV_U64(a);
  const uint8_t * p1 = (const uint8_t *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int r = rsg_rijndael_key_setup_dec(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100002080 rsg_rijndael_encrypt */
void F_100002080(rc_ctx *c) {
  OV_ARGS(a, c);
  const uint32_t * p0 = (const uint32_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const uint8_t * p2 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint8_t * p3 = (uint8_t *)(uintptr_t)OV_U64(a);
  rsg_rijndael_encrypt(p0, p1, p2, p3);
  ov_ret_void(c);
}

/* 0x100002df0 rsg_rijndael_decrypt */
void F_100002df0(rc_ctx *c) {
  OV_ARGS(a, c);
  const uint32_t * p0 = (const uint32_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const uint8_t * p2 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint8_t * p3 = (uint8_t *)(uintptr_t)OV_U64(a);
  rsg_rijndael_decrypt(p0, p1, p2, p3);
  ov_ret_void(c);
}

/* 0x1001eaa70 rsg_bpe_expand */
void F_1001eaa70(rc_ctx *c) {
  OV_ARGS(a, c);
  const uint8_t * p0 = (const uint8_t *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  uint8_t * p2 = (uint8_t *)(uintptr_t)OV_U64(a);
  int r = rsg_bpe_expand(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x101376328 rsg_cryptopp_aes_ctr_decrypt */
void F_101376328(rc_ctx *c) {
  OV_ARGS(a, c);
  const uint8_t * p0 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint64_t p1 = (uint64_t)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  uint32_t p3 = (uint32_t)OV_U64(a);
  uint8_t * p4 = (uint8_t *)(uintptr_t)OV_U64(a);
  uint32_t p5 = (uint32_t)OV_U64(a);
  _Bool r = rsg_cryptopp_aes_ctr_decrypt(p0, p1, p2, p3, p4, p5);
  OV_RET_I32(c, r);
}

/* 0x10137683d rsg_cryptopp_dsa_sha1_verify */
void F_10137683d(rc_ctx *c) {
  OV_ARGS(a, c);
  const uint8_t * p0 = (const uint8_t *)(uintptr_t)OV_U64(a);
  uint32_t p1 = (uint32_t)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  _Bool r = rsg_cryptopp_dsa_sha1_verify(p0, p1, p2);
  OV_RET_I32(c, r);
}
