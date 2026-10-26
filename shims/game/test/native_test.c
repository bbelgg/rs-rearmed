// Native test (arm64): the arm64 rewrites must reproduce the original
// game's results, which harness/difftest.c recorded into a golden file by
// running the original x86_64 code.
//
// Usage: rs_game_native_test GOLDEN PLAIN_BINARY [GAME_DIR]
// PLAIN_BINARY is read as data only (the crypto tests take their keys from it,
// as the original code does); GAME_DIR as for the differential test.
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "crypto_rewrite.h"
#include "efd_classes.h"
#include "efd_rewrite.h"
#include "jpeg_rewrite.h"
#include "lua_rewrite.h"
#include "online_cases.h"
#include "png_rewrite.h"
#include "rs_game.h"
#include "vectors.h"

// The game image as a file: unslid address -> bytes, from the segment table.
static uint8_t *g_img;
static size_t g_img_len;
static const uint8_t *file_at(uint64_t vm) {
  const struct mach_header_64 *mh = (const void *)g_img;
  const uint8_t *lc = g_img + sizeof *mh;
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    const struct segment_command_64 *sg = (const void *)lc;
    if (sg->cmd == LC_SEGMENT_64 && vm >= sg->vmaddr && vm < sg->vmaddr + sg->filesize)
      return g_img + sg->fileoff + (vm - sg->vmaddr);
  }
  static const uint8_t zero[64];
  return zero;
}
static int load_image(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  g_img_len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  g_img = malloc(g_img_len);
  size_t got = fread(g_img, 1, g_img_len, f);
  fclose(f);
  return got == g_img_len && ((const struct mach_header_64 *)g_img)->magic == MH_MAGIC_64 ? 0 : -1;
}

// Slot 28's empty object, on arm64 a stand-in with the refcount slots.
typedef struct {
  void **vtbl;
  int refs;
} fake_obj;
static void fo_inc(void *o) { ((fake_obj *)o)->refs++; }
static void fo_dec(void *o) { ((fake_obj *)o)->refs--; }
static void *fo_vtbl[2] = {(void *)fo_inc, (void *)fo_dec};
static fake_obj the_obj = {fo_vtbl, 0};
static void *new_fake_obj(void *ctx) {
  (void)ctx;
  the_obj.refs = 1;
  return &the_obj;
}

int main(int argc, char **argv) {
  if (argc != 3 && argc != 4) {
    fprintf(stderr, "usage: %s GOLDEN PLAIN_BINARY [GAME_DIR]\n", argv[0]);
    return 2;
  }
  if (load_image(argv[2])) {
    fprintf(stderr, "%s: cannot read the game image\n", argv[2]);
    return 2;
  }
  FILE *g = fopen(argv[1], "r");
  if (!g) {
    perror(argv[1]);
    return 2;
  }
  int checks = 0, failures = 0;
  char name[300];
  for (int i = 0; i < VEC_NAMES; i++) {
    int idx;
    unsigned want;
    if (fscanf(g, " N %d %x", &idx, &want) != 2 || idx != i) {
      fprintf(stderr, "golden file truncated at name %d\n", i);
      return 2;
    }
    vec_name(name, 259, i);
    uint32_t got = rsg_ak_get_id_from_string(name);
    checks++;
    if (got != want && failures++ < 20) fprintf(stderr, "FAIL ak(\"%s\") %08x != %08x\n", name, got, want);
  }
  uint8_t *blob = malloc(VEC_BLOB_MAX), *comp = malloc(compressBound(VEC_BLOB_MAX));
  uint8_t *out = malloc(VEC_BLOB_MAX + 64);
  for (int i = 0; i < VEC_BLOBS; i++) {
    int idx, ret;
    unsigned long crc, adl, outcrc;
    if (fscanf(g, " C %d %lx %lx %d %lx", &idx, &crc, &adl, &ret, &outcrc) != 5 || idx != i) {
      fprintf(stderr, "golden file truncated at blob %d\n", i);
      return 2;
    }
    vec_blob c;
    vec_blob_case(i, blob, &c);
    uLongf clen = compressBound(VEC_BLOB_MAX);
    compress2(comp, &clen, blob, c.n, c.level);
    if (c.corrupt && clen) comp[c.pos % clen] ^= 0x5a;
    uLongf olen = VEC_BLOB_MAX + 64;
    int r = uncompress(out, &olen, comp, clen);
    unsigned long oc = r == Z_OK ? crc32(0, out, (uInt)olen) : 0;
    checks += 3;
    if (crc32(0, blob, (uInt)c.n) != crc && failures++ < 20) fprintf(stderr, "FAIL crc32 %d\n", i);
    if (adler32(1, blob, (uInt)c.n) != adl && failures++ < 20) fprintf(stderr, "FAIL adler32 %d\n", i);
    if ((r != ret || oc != outcrc) && failures++ < 20)
      fprintf(stderr, "FAIL uncompress %d: %d/%08lx vs original %d/%08lx\n", i, r, oc, ret, outcrc);
  }
  char src[2048];
  for (int i = 0; i < LUA_CASES; i++) {
    int idx, load, run;
    unsigned long rcrc, dcrc;
    size_t rlen, dlen;
    if (fscanf(g, " L %d %d %d %lx %zu %lx %zu", &idx, &load, &run, &rcrc, &rlen, &dcrc, &dlen) != 7 ||
        idx != i) {
      fprintf(stderr, "golden file truncated at lua case %d\n", i);
      return 2;
    }
    const char *code = src;
    if (i < LUA_FIXED_CASES) code = lua_fixed_cases[i];
    else lua_gen_case(src, sizeof src, i - LUA_FIXED_CASES);
    lua_outcome o;
    lua_run_case(&lua_rewrite_api, code, &o);
    checks += 2;
    if ((o.load != load || o.run != run || o.result_len != rlen ||
         crc32(0, (const uint8_t *)o.result, (uInt)o.result_len) != rcrc) && failures++ < 20)
      fprintf(stderr, "FAIL lua case %d: %d/%d %s\n  %s\n", i, o.load, o.run, o.result, code);
    if ((o.dump_len != dlen || crc32(0, (const uint8_t *)o.dump, (uInt)o.dump_len) != dcrc) &&
        failures++ < 20)
      fprintf(stderr, "FAIL lua case %d: bytecode differs\n  %s\n", i, code);
    lua_free_outcome(&o);
  }
  for (int i = 0; i < PNG_CASES; i++) {
    uint8_t *file;
    size_t n = png_gen_case(i, &file);
    for (int mode = 0; mode < 2; mode++) {
      int idx, m, stage, depth, type, il, depth2, type2;
      unsigned w, h, w2, h2;
      unsigned long fcrc, mcrc;
      unsigned pcrc;
      size_t rb;
      if (fscanf(g, " P %d %d %lx %d %u %u %d %d %d %u %u %d %d %zu %x %lx", &idx, &m, &fcrc, &stage,
                 &w, &h, &depth, &type, &il, &w2, &h2, &depth2, &type2, &rb, &pcrc, &mcrc) != 16 ||
          idx != i || m != mode) {
        fprintf(stderr, "golden file truncated at png case %d\n", i);
        return 2;
      }
      checks += 2;
      if (crc32(0, file, (uInt)n) != fcrc && failures++ < 20)
        fprintf(stderr, "FAIL png case %d: encoder output differs from the x86_64 run\n", i);
      png_outcome o;
      png_decode(&png_rewrite_api, file, n, !mode, &o);
      if ((o.stage != stage || o.w != w || o.h != h || o.depth != depth || o.type != type ||
           o.interlace != il || o.w2 != w2 || o.h2 != h2 || o.depth2 != depth2 || o.type2 != type2 ||
           o.rowbytes != rb || o.crc != pcrc ||
           crc32(0, (const uint8_t *)o.msgs, (uInt)strlen(o.msgs)) != mcrc) && failures++ < 20)
        fprintf(stderr, "FAIL png case %d mode %d: stage %d crc %08x [%s]\n", i, mode, o.stage, o.crc,
                o.msgs);
    }
    free(file);
  }
  for (int i = 0; i < JPEG_FILES; i++) {
    uint8_t *file;
    size_t n = jpeg_gen_case(i, &file);
    int scale = (int)(1 + vec_next() % 16);
    for (int mode = 0; mode < JPEG_MODES; mode++) {
      int idx, m, v[13];
      unsigned d[4], pcrc;
      unsigned long fcrc, mcrc;
      if (fscanf(g, " J %d %d %lx %d %d %u %u %u %u %d %d %d %d %d %d %d %d %x %lx", &idx, &m, &fcrc,
                 &v[0], &v[1], &d[0], &d[1], &d[2], &d[3], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                 &v[8], &v[9], &pcrc, &mcrc) != 19 || idx != i || m != mode) {
        fprintf(stderr, "golden file truncated at jpeg file %d\n", i);
        return 2;
      }
      checks += 2;
      if (crc32(0, file, (uInt)n) != fcrc && failures++ < 20)
        fprintf(stderr, "FAIL jpeg file %d: encoder output differs from the x86_64 run\n", i);
      jpeg_outcome o;
      jpeg_decode(&jpeg_rewrite_api, file, n, mode, scale, &o);
      if ((o.stage != v[0] || o.header != v[1] || o.w != d[0] || o.h != d[1] || o.ow != d[2] ||
           o.oh != d[3] || o.comps != v[2] || o.jcs != v[3] || o.progressive != v[4] ||
           o.arith != v[5] || o.jfif != v[6] || o.adobe != v[7] || o.ocomps != v[8] ||
           o.ocs != v[9] || o.crc != pcrc ||
           crc32(0, (const uint8_t *)o.msgs, (uInt)strlen(o.msgs)) != mcrc) && failures++ < 20)
        fprintf(stderr, "FAIL jpeg file %d mode %d: stage %d crc %08x [%s]\n", i, mode, o.stage,
                o.crc, o.msgs);
    }
    free(file);
  }
  // efd: transcripts by CRC, then QueryInterface on the class tables the
  // original image had (they are in the golden file).
  efd_rewrite_start();
  for (int gr = 0; gr < EFD_GROUPS; gr++)
    for (int i = 0; i < efd_group_cases[gr]; i++) {
      int g2, i2, lines;
      unsigned long crc;
      if (fscanf(g, " E %d %d %d %lx", &g2, &i2, &lines, &crc) != 4 || g2 != gr || i2 != i) {
        fprintf(stderr, "golden file truncated at efd group %d case %d\n", gr, i);
        return 2;
      }
      efd_run_case(&efd_rewrite_api, gr, i);
      checks += lines;
      if ((efd_tx_lines != lines || crc32(0, (const uint8_t *)efd_tx, (uInt)efd_tx_len) != crc) &&
          failures++ < 20)
        fprintf(stderr, "FAIL efd %s case %d:\n%s", efd_group_names[gr], i, efd_tx);
    }
  for (int c = 0; c < EFD_CLASSES; c++) {
    int c2, m, shared;
    unsigned cnt, oid;
    unsigned long long adjust;
    static uint8_t desc[8 + 0x20 * 32];
    uint32_t ids[32], q[160];
    if (fscanf(g, " Q %d %u %llx", &c2, &cnt, &adjust) != 3 || c2 != c || cnt < 1 || cnt > 32) {
      fprintf(stderr, "golden file truncated at efd class %d\n", c);
      return 2;
    }
    memset(desc, 0, sizeof desc);
    *(uint32_t *)desc = cnt;
    for (unsigned e = 0; e < cnt; e++) {
      unsigned off;
      if (fscanf(g, " %x %x", &ids[e], &off) != 2) return 2;
      *(uint32_t *)(desc + 8 + 0x20 * e) = ids[e];
      *(uint32_t *)(desc + 8 + 0x20 * e + 0x18) = off;
    }
    if (fscanf(g, " %x %d %d", &oid, &shared, &m) != 3 || m != efd_qi_queries(ids, (int)cnt, q)) return 2;
    checks++;
    if (shared && rsg_efd_class_desc_id(desc) != oid && failures++ < 20)
      fprintf(stderr, "FAIL efd class %s: GetClassID\n", efd_classes[c].name);
    static uint64_t mem[1024];
    uint8_t *obj = (uint8_t *)mem + 0x1000, *base = obj - adjust;  // `this` of the secondary base, the object
    for (int j = 0; j < m; j++) {
      int r;
      unsigned long long off;
      if (fscanf(g, " %d %llx", &r, &off) != 2) return 2;
      void *w = (void *)1;
      bool rw = rsg_efd_query_interface(desc, base, q[j], &w);
      unsigned long long woff = w ? (unsigned long long)((uint8_t *)w - obj) : ~0ull;
      checks++;
      if ((rw != r || woff != off) && failures++ < 20)
        fprintf(stderr, "FAIL efd class %s: QueryInterface(%x)\n", efd_classes[c].name, q[j]);
    }
  }
  // Crypto: the same cases, transcripts by CRC.
  crypto_keys_load(file_at);
  cr_dsa_keys_init();
  crypto_real_init(argc > 3 ? argv[3] : NULL);
  int kp, ks, kk;
  if (fscanf(g, " KR %d %d %d", &kp, &ks, &kk) != 3) {
    fprintf(stderr, "golden file truncated at the crypto tests\n");
    return 2;
  }
  checks++;
  if ((kp != cr_npsarcs || ks != cr_nsngs || kk != (cr_pkg.psarc >= 0)) && failures++ < 20)
    fprintf(stderr, "FAIL crypto: game data differs from the x86_64 run (%d/%d/%d vs %d/%d/%d)\n", cr_npsarcs,
            cr_nsngs, cr_pkg.psarc >= 0, kp, ks, kk);
  for (int gr = 0; gr < CR_GROUPS; gr++)
    for (int i = 0; i < crypto_group_cases(gr); i++) {
      int g2, i2, lines;
      unsigned long crc;
      if (fscanf(g, " K %d %d %d %lx", &g2, &i2, &lines, &crc) != 4 || g2 != gr || i2 != i) {
        fprintf(stderr, "golden file truncated at crypto group %d case %d\n", gr, i);
        return 2;
      }
      crypto_run_case(&crypto_rewrite_api, gr, i);
      checks += lines;
      if ((efd_tx_lines != lines || crc32(0, (const uint8_t *)efd_tx, (uInt)efd_tx_len) != crc) &&
          failures++ < 20)
        fprintf(stderr, "FAIL crypto %s case %d:\n%s", crypto_group_names[gr], i, efd_tx);
    }
  // The online stub: its transcript must be the original's.
  online_fakes_init();
  rsg_online_config cfg = {online_game_interface, new_fake_obj, NULL};
  void *stub = rsg_online_extension_create(&cfg);
  int olines, xlines;
  unsigned long ocrc, xcrc;
  if (fscanf(g, " O %d %lx X %d %lx", &olines, &ocrc, &xlines, &xcrc) != 4) {
    fprintf(stderr, "golden file truncated at the online tests\n");
    return 2;
  }
  online_ext_transcript(stub, online_stub_login);
  checks += olines;
  if ((efd_tx_lines != olines || crc32(0, (const uint8_t *)efd_tx, (uInt)efd_tx_len) != ocrc) && failures++ < 20)
    fprintf(stderr, "FAIL online stub transcript:\n%s", efd_tx);
  online_stub_extra(stub);
  checks += xlines;
  if ((efd_tx_lines != xlines || crc32(0, (const uint8_t *)efd_tx, (uInt)efd_tx_len) != xcrc) && failures++ < 20)
    fprintf(stderr, "FAIL online stub (stub-only requests):\n%s", efd_tx);
  checks += 2;
  if (the_obj.refs != 1 && failures++ < 20) fprintf(stderr, "FAIL online stub: slot 28 object refs %d\n", the_obj.refs);
  EXT_FN(stub, 2, ext_void0)(stub);  // destroys it
  if (the_obj.refs != 0 && failures++ < 20) fprintf(stderr, "FAIL online stub: object not released\n");

  printf("%d checks, %d passed, %d failed\n", checks, checks - failures, failures);
  return failures ? 1 : 0;
}
