// The local crypto the game needs offline.
//
// Arm64 rewrites of the Crypto++ 5.6 wrappers the game calls, of the game's
// own copy of the Rijndael reference code, of its byte-pair-encoding key
// unpacker and of the block loops around them (profile saves, the package
// config). Each function keeps the original's signature and its behaviour on
// every input the differential test covers (harness/difftest.c, "crypto").
//
// Keys and IVs are never compiled in. The originals read them from the game
// image (addresses below); the rewrites take them as arguments, and the tests
// read them from the image at runtime.
//
// AES is self-contained (the reference algorithm, tables built at first use);
// SHA-1 is CommonCrypto's; DSA uses a small bignum (src/crypto_bn.h).
#ifndef RS_CRYPTO_H
#define RS_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- where the originals and their keys are (unslid image addresses) ---------
#define RS_ORIG_RIJNDAEL_KEY_SETUP_ENC 0x100001b40
#define RS_ORIG_RIJNDAEL_KEY_SETUP_DEC 0x100001eb0
#define RS_ORIG_RIJNDAEL_ENCRYPT 0x100002080
#define RS_ORIG_RIJNDAEL_DECRYPT 0x100002df0
#define RS_ORIG_BPE_EXPAND 0x1001eaa70
#define RS_ORIG_CRYPTOPP_AES_CTR 0x101376328
#define RS_ORIG_CRYPTOPP_DSA_VERIFY 0x10137683d
#define RS_ORIG_CRYPTOPP_AES_CFB 0x101376f8f
#define RS_ORIG_PROFILE_ENCRYPT 0x100168c90 /* save: (buf, int *len) */
#define RS_ORIG_PROFILE_LOAD 0x100168df0    /* load: (?, path, ctx, callback) */
#define RS_ORIG_PKGCONFIG_DECRYPT 0x1002a7550
#define RS_ORIG_SNG_LOAD 0x1001eb6a0
#define RS_ORIG_PSARC_OPEN 0x100161780
#define RS_ORIG_PREVIEW_DECRYPT 0x100a2aeb0
#define RS_RANGE_LIB_CRYPTOPP 0x101343dd0ULL, 0x101383700ULL /* its 2 static initializers */

// Key material in the image (never copied into source, logs or golden files).
#define RS_KEY_SNG_DSA_BLOB_LEN 0x101878508 /* u32 */
#define RS_KEY_SNG_DSA_BLOB 0x101878510     /* BPE -> 1,680 hex chars, buffer 0x691 */
#define RS_KEY_SNG_AES_BLOB_LEN 0x1018784b0
#define RS_KEY_SNG_AES_BLOB 0x1018784c0 /* BPE -> 64 hex chars, buffer 0x41 */
#define RS_KEY_PKG_DSA_BLOB_LEN 0x10187aa70
#define RS_KEY_PKG_DSA_BLOB 0x10187aa80 /* BPE -> 1,678 hex chars, buffer 0x68f */
#define RS_KEY_PKG_AES 0x1013ecc60      /* 32 bytes */
#define RS_KEY_PSARC_HEX 0x1013eba80    /* 64 hex chars + NUL (the caller passes 0x41) */
#define RS_KEY_PSARC_IV 0x10142e3d0     /* 16 bytes */
#define RS_KEY_PROFILE 0x101877060      /* 32 bytes */
#define RS_KEY_PREVIEW 0x1013f9a90      /* 32 bytes */

// --- rijndael-alg-fst.c v3.0 (the game's copy, 0x100001b40...) ---------------
// Same contract: rk holds 4*(Nr+1) words (60 for 256-bit keys); the setup
// functions return Nr (10/12/14) or 0 for an unsupported keyBits, after
// writing the first 4, 6 or 8 words exactly as the reference code does.
int rsg_rijndael_key_setup_enc(uint32_t rk[], const uint8_t key[], int key_bits);
int rsg_rijndael_key_setup_dec(uint32_t rk[], const uint8_t key[], int key_bits);
void rsg_rijndael_encrypt(const uint32_t rk[], int nr, const uint8_t pt[16], uint8_t ct[16]);
void rsg_rijndael_decrypt(const uint32_t rk[], int nr, const uint8_t ct[16], uint8_t pt[16]);

// --- the three Crypto++ wrappers ---------------------------------------------
// Key arguments are hex text, decoded like Crypto++'s HexDecoder: either case,
// every other character skipped, a trailing odd digit dropped. AES accepts
// 16/24/32-byte keys; any other length is the original's InvalidKeyLength
// exception, caught by the wrapper: return false and leave `out` untouched.

// 0x101376328, SNG payload: AES-CTR. The counter block is data[0..16)
// (big-endian 128-bit increment); data[16..len) is decrypted into out, of
// which at most out_len bytes are written. len must be >= 16 (the original
// computes len - 16 in 32 bits). Returns true unless the key is invalid.
bool rsg_cryptopp_aes_ctr_decrypt(const uint8_t *data, uint64_t len, const char *key_hex,
                                  uint32_t key_hex_len, uint8_t *out, uint32_t out_len);

// 0x101376f8f, PSARC table of contents: AES-CFB (128-bit feedback), any
// length, in == out allowed. The original takes the IV from its own
// __TEXT,__const (RS_KEY_PSARC_IV); here the caller passes it.
bool rsg_cryptopp_aes_cfb_decrypt(const uint8_t *in, uint32_t len, const char *key_hex,
                                  uint32_t key_hex_len, uint8_t *out, const uint8_t iv[16]);

// 0x10137683d, SNG and package-config signatures: Crypto++ DSA2<SHA1>
// verify. key_hex is a NUL-terminated hex X.509 SubjectPublicKeyInfo (DSA
// OID, p/q/g parameters, y); the signature is the last 2*bytes(q) bytes of
// data (r || s, big-endian), and SHA-1 covers the rest. A key that does not
// decode is the original's BERDecodeErr, caught: false. Limits (the original
// is undefined or untested there): len shorter than the signature, negative
// DER integers, an even p, and p above 4,096 bits give false.
bool rsg_cryptopp_dsa_sha1_verify(const uint8_t *data, uint32_t len, const char *key_hex);

// --- game code around the crypto ----------------------------------------------
// 0x1001eaa70: Philip Gage's byte-pair-encoding expander (1994), every output
// byte NOTed. Returns the bytes written. Like the original it trusts the
// stream; unlike it, a malformed pair table or a pair chain deeper than the
// original's 32-byte stack stops the expansion instead of corrupting memory.
int rsg_bpe_expand(const uint8_t *src, int n, uint8_t *out);

// 0x100168c90, profile save: AES-256-ECB in place, the last partial block
// padded with a 0 byte and then rnd() bytes (the original calls rand()). *len
// becomes the padded length; len 0 becomes 16 without touching buf. buf must
// have room for the padding.
void rsg_profile_encrypt(uint8_t *buf, int *len, const uint8_t key[32], int (*rnd)(void));

// 0x100168df0, profile load, after the file has been read (fopen/fread and
// the Steam-cloud read stay game code). file is the whole *_PRFLDB file:
// "EVAS", u32 version (1), u64 owner (checked only for cloud files), u32
// uncompressed size, then AES-256-ECB(zlib data). Decrypts in place.
// Returns the original's callback code: 0 ok (*out malloc'd, *out_len),
// 4 malformed (bad magic/size/zlib), 6 another user's cloud file, 7 newer
// version.
enum { RSG_PROFILE_OK = 0, RSG_PROFILE_BAD = 4, RSG_PROFILE_OTHER_USER = 6, RSG_PROFILE_TOO_NEW = 7 };
int rsg_profile_decode(uint8_t *file, uint32_t size, const uint8_t key[32], bool from_cloud,
                       uint64_t user_id, uint8_t **out, uint32_t *out_len);

// 0x1002a7550, the package-config callback: DSA verify (last 0x38 bytes; a
// failure is only logged by the game, *verified = false), then AES-256-ECB
// decrypt in place of every whole 16-byte block of the signed region (the
// whole buffer when the check failed). Returns the text length: up to the
// first NUL within that region.
uint32_t rsg_pkgconfig_decrypt(uint8_t *buf, uint32_t len, const char *dsa_key_hex,
                               const uint8_t key[32], bool *verified);

// AES-256-ECB over whole blocks (the loop shared by the package config and
// the DLC preview metadata 0x100a2aeb0); a trailing partial block is left.
void rsg_aes256_ecb_decrypt_blocks(const uint8_t key[32], uint8_t *buf, size_t len);
void rsg_aes256_ecb_encrypt_blocks(const uint8_t key[32], uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
