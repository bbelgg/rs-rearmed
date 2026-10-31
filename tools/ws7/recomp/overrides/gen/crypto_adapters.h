// Adapter table of override module `crypto` (generated, checked in).
#ifndef OV_GEN_CRYPTO_H
#define OV_GEN_CRYPTO_H
#include "rc.h"
#include "rc_ov.h"
void RC_FN(rsg_rijndael_key_setup_enc)(rc_ctx *c);
void RC_FN(rsg_rijndael_key_setup_dec)(rc_ctx *c);
void RC_FN(rsg_rijndael_encrypt)(rc_ctx *c);
void RC_FN(rsg_rijndael_decrypt)(rc_ctx *c);
void RC_FN(rsg_bpe_expand)(rc_ctx *c);
void RC_FN(rsg_cryptopp_aes_ctr_decrypt)(rc_ctx *c);
void RC_FN(rsg_cryptopp_dsa_sha1_verify)(rc_ctx *c);
void RC_FN(rsg_cryptopp_aes_cfb_decrypt)(rc_ctx *c);
static const ov_entry ov_crypto_entries[] = {
  {RC_ADDR(rsg_rijndael_key_setup_enc), "rsg_rijndael_key_setup_enc", RC_FN(rsg_rijndael_key_setup_enc)},
  {RC_ADDR(rsg_rijndael_key_setup_dec), "rsg_rijndael_key_setup_dec", RC_FN(rsg_rijndael_key_setup_dec)},
  {RC_ADDR(rsg_rijndael_encrypt), "rsg_rijndael_encrypt", RC_FN(rsg_rijndael_encrypt)},
  {RC_ADDR(rsg_rijndael_decrypt), "rsg_rijndael_decrypt", RC_FN(rsg_rijndael_decrypt)},
  {RC_ADDR(rsg_bpe_expand), "rsg_bpe_expand", RC_FN(rsg_bpe_expand)},
  {RC_ADDR(rsg_cryptopp_aes_ctr_decrypt), "rsg_cryptopp_aes_ctr_decrypt", RC_FN(rsg_cryptopp_aes_ctr_decrypt)},
  {RC_ADDR(rsg_cryptopp_dsa_sha1_verify), "rsg_cryptopp_dsa_sha1_verify", RC_FN(rsg_cryptopp_dsa_sha1_verify)},
  {RC_ADDR(rsg_cryptopp_aes_cfb_decrypt), "rsg_cryptopp_aes_cfb_decrypt", RC_FN(rsg_cryptopp_aes_cfb_decrypt)},
};
#define OV_CRYPTO_COUNT 8
#endif
