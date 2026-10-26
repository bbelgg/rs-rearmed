// Adapter table of override module `crypto` (generated, checked in).
#ifndef OV_GEN_CRYPTO_H
#define OV_GEN_CRYPTO_H
#include "rc.h"
#include "rc_ov.h"
void F_100001b40(rc_ctx *c);
void F_100001eb0(rc_ctx *c);
void F_100002080(rc_ctx *c);
void F_100002df0(rc_ctx *c);
void F_1001eaa70(rc_ctx *c);
void F_101376328(rc_ctx *c);
void F_10137683d(rc_ctx *c);
void F_101376f8f(rc_ctx *c);
static const ov_entry ov_crypto_entries[] = {
  {0x100001b40ull, "rsg_rijndael_key_setup_enc", F_100001b40},
  {0x100001eb0ull, "rsg_rijndael_key_setup_dec", F_100001eb0},
  {0x100002080ull, "rsg_rijndael_encrypt", F_100002080},
  {0x100002df0ull, "rsg_rijndael_decrypt", F_100002df0},
  {0x1001eaa70ull, "rsg_bpe_expand", F_1001eaa70},
  {0x101376328ull, "rsg_cryptopp_aes_ctr_decrypt", F_101376328},
  {0x10137683dull, "rsg_cryptopp_dsa_sha1_verify", F_10137683d},
  {0x101376f8full, "rsg_cryptopp_aes_cfb_decrypt", F_101376f8f},
};
#define OV_CRYPTO_COUNT 8
#endif
