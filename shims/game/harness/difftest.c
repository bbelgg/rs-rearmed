// Differential test (x86_64, runs under Rosetta).
//
// Calls original game functions through rsimage and the arm64 rewrites on the
// same inputs and compares. Writes every result to a golden file that the
// arm64 test (test/native_test.c) checks the arm64 rewrites against.
//
// Usage: rs_game_difftest PLAIN_BINARY GOLDEN_OUT [GAME_DIR]
// GAME_DIR (the Rocksmith2014 install, read-only) adds the real-data crypto
// cases: PSARC tables of contents, .sng files and pkgconfig.ini.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "../include/rs_game.h"
#include "../test/lua_rewrite.h"
#include "../test/jpeg_rewrite.h"
#include "../test/png_rewrite.h"
#include "../test/vectors.h"
#include "../test/efd_classes.h"
#include "../test/efd_rewrite.h"
#include "../test/crypto_rewrite.h"
#include "../test/online_cases.h"
#include "../include/rs_online.h"
#include "efd_env.h"
#include "rsimage.h"

typedef uint32_t (*ak_hash_fn)(const char *);
typedef unsigned long (*sum_fn)(unsigned long, const uint8_t *, unsigned);
typedef int (*uncompress_fn)(uint8_t *, unsigned long *, const uint8_t *, unsigned long);
typedef int (*inflate_init_fn)(z_stream *, const char *, int);
typedef int (*inflate_fn)(z_stream *, int);
typedef int (*inflate_end_fn)(z_stream *);

static int failures, checks;

#define CHECK(cond, ...)                \
  do {                                  \
    checks++;                           \
    if (!(cond)) {                      \
      if (failures++ < 20) {            \
        fprintf(stderr, "FAIL: ");      \
        fprintf(stderr, __VA_ARGS__);   \
        fprintf(stderr, "\n");          \
      }                                 \
    }                                   \
  } while (0)

// The original image's Lua behind the same lua_api as the rebuilt one.
static lua_api orig_lua_api(void) {
  lua_api a = {
      .newstate = RS_FN(__typeof__(((lua_api *)0)->newstate), RS_ORIG_LUA_NEWSTATE),
      .close = RS_FN(__typeof__(((lua_api *)0)->close), RS_ORIG_LUA_CLOSE),
      .open = {RS_FN(lua_cfn, RS_ORIG_LUAOPEN_BASE), RS_FN(lua_cfn, RS_ORIG_LUAOPEN_PACKAGE),
               RS_FN(lua_cfn, RS_ORIG_LUAOPEN_TABLE), RS_FN(lua_cfn, RS_ORIG_LUAOPEN_OS),
               RS_FN(lua_cfn, RS_ORIG_LUAOPEN_STRING), RS_FN(lua_cfn, RS_ORIG_LUAOPEN_MATH),
               RS_FN(lua_cfn, RS_ORIG_LUAOPEN_DEBUG)},
      .pushcclosure = RS_FN(__typeof__(((lua_api *)0)->pushcclosure), RS_ORIG_LUA_PUSHCCLOSURE),
      .pushstring = RS_FN(__typeof__(((lua_api *)0)->pushstring), RS_ORIG_LUA_PUSHSTRING),
      .call = RS_FN(__typeof__(((lua_api *)0)->call), RS_ORIG_LUA_CALL),
      .loadbuffer = RS_FN(__typeof__(((lua_api *)0)->loadbuffer), RS_ORIG_LUAL_LOADBUFFER),
      .pcall = RS_FN(__typeof__(((lua_api *)0)->pcall), RS_ORIG_LUA_PCALL),
      .gettop = RS_FN(__typeof__(((lua_api *)0)->gettop), RS_ORIG_LUA_GETTOP),
      .settop = RS_FN(__typeof__(((lua_api *)0)->settop), RS_ORIG_LUA_SETTOP),
      .insert = RS_FN(__typeof__(((lua_api *)0)->insert), RS_ORIG_LUA_INSERT),
      .getfield = RS_FN(__typeof__(((lua_api *)0)->getfield), RS_ORIG_LUA_GETFIELD),
      .pushlstring = RS_FN(__typeof__(((lua_api *)0)->pushlstring), RS_ORIG_LUA_PUSHLSTRING),
      .tolstring = RS_FN(__typeof__(((lua_api *)0)->tolstring), RS_ORIG_LUA_TOLSTRING),
  };
  return a;
}

#define ORIG_PNG(field, addr) .field = RS_FN(__typeof__(((png_api *)0)->field), addr)
static png_api orig_png_api(void) {
  png_api a = {
      ORIG_PNG(create_read_struct, RS_ORIG_PNG_CREATE_READ_STRUCT),
      ORIG_PNG(create_info_struct, RS_ORIG_PNG_CREATE_INFO_STRUCT),
      ORIG_PNG(destroy_read_struct, RS_ORIG_PNG_DESTROY_READ_STRUCT),
      ORIG_PNG(set_read_fn, RS_ORIG_PNG_SET_READ_FN),
      ORIG_PNG(set_sig_bytes, RS_ORIG_PNG_SET_SIG_BYTES),
      ORIG_PNG(sig_cmp, RS_ORIG_PNG_SIG_CMP),
      ORIG_PNG(read_info, RS_ORIG_PNG_READ_INFO),
      ORIG_PNG(get_IHDR, RS_ORIG_PNG_GET_IHDR),
      ORIG_PNG(get_valid, RS_ORIG_PNG_GET_VALID),
      ORIG_PNG(get_gAMA, RS_ORIG_PNG_GET_GAMA),
      ORIG_PNG(get_rowbytes, RS_ORIG_PNG_GET_ROWBYTES),
      ORIG_PNG(set_strip_16, RS_ORIG_PNG_SET_STRIP_16),
      ORIG_PNG(set_palette_to_rgb, RS_ORIG_PNG_SET_PALETTE_TO_RGB),
      ORIG_PNG(set_expand_gray_1_2_4_to_8, RS_ORIG_PNG_SET_EXPAND_GRAY_1_2_4_TO_8),
      ORIG_PNG(set_tRNS_to_alpha, RS_ORIG_PNG_SET_TRNS_TO_ALPHA),
      ORIG_PNG(set_gray_to_rgb, RS_ORIG_PNG_SET_GRAY_TO_RGB),
      ORIG_PNG(set_gamma, RS_ORIG_PNG_SET_GAMMA),
      ORIG_PNG(read_update_info, RS_ORIG_PNG_READ_UPDATE_INFO),
      ORIG_PNG(read_image, RS_ORIG_PNG_READ_IMAGE),
      ORIG_PNG(read_row, RS_ORIG_PNG_READ_ROW),
      ORIG_PNG(read_end, RS_ORIG_PNG_READ_END),
  };
  return a;
}

static int png_same(const png_outcome *a, const png_outcome *b) {
  return a->stage == b->stage && a->w == b->w && a->h == b->h && a->depth == b->depth &&
         a->type == b->type && a->interlace == b->interlace && a->w2 == b->w2 && a->h2 == b->h2 &&
         a->depth2 == b->depth2 && a->type2 == b->type2 && a->rowbytes == b->rowbytes &&
         a->crc == b->crc && strcmp(a->msgs, b->msgs) == 0;
}

#define ORIG_JPEG(field, addr) .field = RS_FN(__typeof__(((jpeg_api *)0)->field), addr)
static jpeg_api orig_jpeg_api(void) {
  jpeg_api a = {
      ORIG_JPEG(std_error, RS_ORIG_JPEG_STD_ERROR),
      ORIG_JPEG(create_decompress, RS_ORIG_JPEG_CREATE_DECOMPRESS),
      ORIG_JPEG(read_header, RS_ORIG_JPEG_READ_HEADER),
      ORIG_JPEG(start_decompress, RS_ORIG_JPEG_START_DECOMPRESS),
      ORIG_JPEG(read_scanlines, RS_ORIG_JPEG_READ_SCANLINES),
      ORIG_JPEG(finish_decompress, RS_ORIG_JPEG_FINISH_DECOMPRESS),
      ORIG_JPEG(destroy_decompress, RS_ORIG_JPEG_DESTROY_DECOMPRESS),
      ORIG_JPEG(read_coefficients, RS_ORIG_JPEG_READ_COEFFICIENTS),
      ORIG_JPEG(resync_to_restart, RS_ORIG_JPEG_RESYNC_TO_RESTART),
  };
  return a;
}

// The original efd behind the same efd_api as the rewrites. The string table
// probes read the original's globals.
static uint64_t orig_st_count(void) { return *RS_FN(uint64_t *, RS_ORIG_EFD_STRING_COUNT); }
static uint64_t orig_st_collisions(void) { return *RS_FN(uint64_t *, RS_ORIG_EFD_STRING_COLLISIONS); }
static uint32_t orig_st_bucket_size(uint32_t b) {
  uint8_t *t = *RS_FN(uint8_t **, RS_ORIG_EFD_STRING_TABLE);
  uint64_t *v = (uint64_t *)(t + 24 * b);  // {begin, end, capacity}
  return (uint32_t)((v[1] - v[0]) / 8);
}
static void orig_sqrt_table(uint32_t *out) {
  uint32_t *t = RS_FN(uint32_t * (*)(void), RS_ORIG_EFD_SQRT_TABLE_NEW)();
  memcpy(out, t, 256 * sizeof *t);
  RS_FN(void (*)(void *, size_t), RS_ORIG_EFD_FREE)(t, (size_t)-1);
}

#define ORIG_EFD(field, addr) .field = RS_FN(__typeof__(((efd_api *)0)->field), addr)
static efd_api orig_efd_api(void) {
  efd_api a = {
      ORIG_EFD(id_set_type, RS_ORIG_EFD_ID_SET_TYPE),
      ORIG_EFD(id_set_usage, RS_ORIG_EFD_ID_SET_USAGE),
      ORIG_EFD(id_set_shard, RS_ORIG_EFD_ID_SET_SHARD),
      ORIG_EFD(id_set_net_id, RS_ORIG_EFD_ID_SET_NET_ID),
      ORIG_EFD(id_set_base_id, RS_ORIG_EFD_ID_SET_BASE_ID),
      ORIG_EFD(cat_u0, RS_ORIG_EFD_CATEGORY_U0),
      ORIG_EFD(cat_u2, RS_ORIG_EFD_CATEGORY_U2),
      ORIG_EFD(cat_svc, RS_ORIG_EFD_CATEGORY_SVC),
      ORIG_EFD(fs_init, RS_ORIG_EFD_FS_INIT),
      ORIG_EFD(fs_init_cstr, RS_ORIG_EFD_FS_INIT_CSTR),
      ORIG_EFD(fs_init_copy, RS_ORIG_EFD_FS_INIT_COPY),
      ORIG_EFD(fs_release, RS_ORIG_EFD_FS_RELEASE),
      ORIG_EFD(fs_assign, RS_ORIG_EFD_FS_ASSIGN),
      ORIG_EFD(fs_c_str, RS_ORIG_EFD_FS_C_STR),
      ORIG_EFD(fs_exists, RS_ORIG_EFD_FS_EXISTS),
      ORIG_EFD(fs_length, RS_ORIG_EFD_FS_LENGTH),
      ORIG_EFD(fs_equals, RS_ORIG_EFD_FS_EQUALS),
      .st_count = orig_st_count,
      .st_collisions = orig_st_collisions,
      .st_bucket_size = orig_st_bucket_size,
      ORIG_EFD(ds_init, RS_ORIG_EFD_DS_INIT),
      ORIG_EFD(ds_init_copy, RS_ORIG_EFD_DS_INIT_COPY),
      ORIG_EFD(ds_destroy, RS_ORIG_EFD_DS_DTOR),
      ORIG_EFD(ds_read, RS_ORIG_EFD_DS_READ),
      ORIG_EFD(ds_write, RS_ORIG_EFD_DS_WRITE),
      ORIG_EFD(ds_seek, RS_ORIG_EFD_DS_SEEK),
      ORIG_EFD(ds_write_stream, RS_ORIG_EFD_DS_WRITE_STREAM),
      ORIG_EFD(ds_read_stream, RS_ORIG_EFD_DS_READ_STREAM),
      ORIG_EFD(ds_write_id, RS_ORIG_EFD_DS_WRITE_ID),
      ORIG_EFD(ds_read_id, RS_ORIG_EFD_DS_READ_ID),
      ORIG_EFD(utf8_units_at, RS_ORIG_EFD_UTF8_UNITS_AT),
      ORIG_EFD(utf8_units, RS_ORIG_EFD_UTF8_UNITS),
      ORIG_EFD(utf8_tail_missing, RS_ORIG_EFD_UTF8_TAIL_MISSING),
      ORIG_EFD(u8_length, RS_ORIG_EFD_UTF8_LENGTH),
      ORIG_EFD(u8_lower, RS_ORIG_EFD_UTF8_TO_LOWER),
      ORIG_EFD(u8_upper, RS_ORIG_EFD_UTF8_TO_UPPER),
      ORIG_EFD(path_standardize, RS_ORIG_EFD_PATH_STANDARDIZE),
      ORIG_EFD(efd_snprintf, RS_ORIG_EFD_SNPRINTF),
      .sqrt_table = orig_sqrt_table,
      ORIG_EFD(mh_init, RS_ORIG_EFD_MSGHELPER_CTOR),
      ORIG_EFD(mh_preinit, RS_ORIG_EFD_MSGHELPER_PREINIT),
      ORIG_EFD(mh_shutdown, RS_ORIG_EFD_MSGHELPER_SHUTDOWN),
      ORIG_EFD(mh_net_id, RS_ORIG_EFD_MSGHELPER_ON_NET_ID),
      ORIG_EFD(mh_on_init, RS_ORIG_EFD_MSGHELPER_INIT),
      ORIG_EFD(mh_on_tick, RS_ORIG_EFD_MSGHELPER_TICK),
      ORIG_EFD(mh_name, RS_ORIG_EFD_MSGHELPER_NAME),
  };
  return a;
}

// Prints the first line where two transcripts differ.
static void print_first_diff(const char *o, const char *w) {
  const char *o0 = o;
  int line = 1;
  while (*o && *o == *w) {
    if (*o == '\n') line++;
    o++;
    w++;
  }
  while (o > o0 && o[-1] != '\n') o--, w--;
  fprintf(stderr, "  line %d\n  original: %.*s\n  rewrite:  %.*s\n", line, (int)strcspn(o, "\n"), o,
          (int)strcspn(w, "\n"), w);
}

// Original efd code vs the rewrites. Writes "E group case lines crc" and, for
// QueryInterface, "Q class n {id offset}... m {id ret offset}..." (the class
// tables are the inputs the arm64 test needs).
static void efd_difftest(FILE *golden) {
  int n = efd_env_start();
  CHECK(n == 83, "efd static initializers: ran %d", n);
  efd_rewrite_start();
  efd_env_set_logger(&efd_logger);
  printf("efd: %d initializers + efd::Init, %llu interned strings before the tests\n", n,
         (unsigned long long)orig_st_count());

  // The table efd::Init built must match the one the rewrite computes.
  uint32_t t[256];
  rsg_efd_fill_sqrt_table(t);
  CHECK(memcmp(t, *RS_FN(uint32_t **, RS_ORIG_EFD_SQRT_TABLE), sizeof t) == 0, "efd::Init sqrt table");

  efd_api orig = orig_efd_api();
  int dump_g = -1, dump_i = -1;  // RS_EFD_DUMP=group:case prints that case's transcript
  if (getenv("RS_EFD_DUMP")) sscanf(getenv("RS_EFD_DUMP"), "%d:%d", &dump_g, &dump_i);
  char *otx = NULL;
  size_t ocap = 0;
  for (int g = 0; g < EFD_GROUPS; g++) {
    int lines = 0, bad = 0;
    for (int i = 0; i < efd_group_cases[g]; i++) {
      efd_run_case(&orig, g, i);
      if (ocap < efd_tx_len + 1) otx = realloc(otx, ocap = efd_tx_len + 1);
      memcpy(otx, efd_tx, efd_tx_len + 1);
      int olines = efd_tx_lines;
      if (g == dump_g && i == dump_i) fprintf(stderr, "%s", otx);
      unsigned long ocrc = crc32(0, (const uint8_t *)otx, (uInt)efd_tx_len);
      efd_run_case(&efd_rewrite_api, g, i);
      int same = strcmp(otx, efd_tx) == 0;
      checks += olines - 1;  // one check per transcript line; CHECK adds the last
      lines += olines;
      CHECK(same, "efd %s case %d differs", efd_group_names[g], i);
      if (!same && bad++ < 3) print_first_diff(otx, efd_tx);
      fprintf(golden, "E %d %d %d %08lx\n", g, i, olines, ocrc);
    }
    printf("efd %-28s %5d cases, %6d transcript lines\n", efd_group_names[g], efd_group_cases[g], lines);
  }
  free(otx);

  // QueryInterface / GetClassID on every efd class. Their class-info tables are
  // filled by static initializers all over the binary, so run the rest of them.
  int all = rsimage_run_inits(RS_RANGE_ALL);
  CHECK(all >= 0, "remaining static initializers");
  printf("efd: %d more initializers (all %d run)\n", all, rsimage_init_count());
  int queries = 0;
  for (int c = 0; c < EFD_CLASSES; c++) {
    const efd_class *k = &efd_classes[c];
    static uint64_t obj[64];  // the methods only adjust `this`
    obj[0] = k->vtable + rsimage_slide;
    const uint8_t *desc = RS_FN(const uint8_t *(*)(void *), k->class_desc)(obj);
    uint32_t cnt = *(const uint32_t *)desc, ids[64];
    CHECK(cnt >= 1 && cnt <= 32, "class %s: %u class-info entries", k->name, cnt);
    if (cnt < 1 || cnt > 32) continue;
    // A secondary vtable's QueryInterface first moves `this` to the complete
    // object by the vtable's offset-to-top (e.g. -0x3f0 for the UIMenu_* shop
    // menus), then adds the entry's offset.
    int64_t top = *(const int64_t *)(k->vtable + rsimage_slide - 16);
    uint8_t *base = (uint8_t *)obj + top;
    fprintf(golden, "Q %d %u %llx", c, cnt, (unsigned long long)-top);
    for (uint32_t e = 0; e < cnt; e++) {
      ids[e] = *(const uint32_t *)(desc + 8 + 0x20 * e);
      fprintf(golden, " %x %x", ids[e], *(const uint32_t *)(desc + 8 + 0x20 * e + 0x18));
    }
    // Most classes share IBase::GetClassID (the table's first ID); a few
    // messages override it with a constant.
    uint32_t oid = RS_FN(uint32_t (*)(void *), k->class_id)(obj);
    int shared = k->class_id == RS_ORIG_EFD_GET_CLASS_ID;
    CHECK(!shared || oid == rsg_efd_class_desc_id(desc), "class %s: GetClassID %x vs %x", k->name, oid,
          rsg_efd_class_desc_id(desc));
    uint32_t q[160];
    int m = efd_qi_queries(ids, (int)cnt, q);
    fprintf(golden, " %x %d %d", oid, shared, m);
    for (int j = 0; j < m; j++) {
      void *o1 = (void *)1, *o2 = (void *)1, *w = (void *)1;
      bool r1 = RS_FN(bool (*)(void *, uint32_t, void **), k->query_interface)(obj, q[j], &o1);
      bool r2 = RS_FN(bool (*)(void *, uint32_t, void **), k->query_interface_const)(obj, q[j], &o2);
      bool rw = rsg_efd_query_interface(desc, base, q[j], &w);
      CHECK(r1 == rw && o1 == w && r2 == rw && o2 == w, "class %s: QueryInterface(%x) %d/%p %d/%p vs %d/%p",
            k->name, q[j], r1, o1, r2, o2, rw, w);
      fprintf(golden, " %d %llx", r1, o1 ? (unsigned long long)((uint8_t *)o1 - (uint8_t *)obj) : ~0ull);
      queries++;
    }
    fprintf(golden, "\n");
  }
  printf("efd QueryInterface               %5d classes, %6d queries\n", EFD_CLASSES, queries);
}

// --- crypto: the originals behind crypto_api -----------------------------------
#define RS_ORIG_EFD_ALLOC 0x1012efc90 /* (size, flags) */
#define RS_ORIG_EFD_FREE_SIZED 0x1012f00f0
#define RS_ORIG_JSON_VALUE_CTOR 0x1000f9ad0

static const uint8_t *img_at(uint64_t vm) { return (const uint8_t *)(uintptr_t)(vm + rsimage_slide); }

// A fake RSConnectService for the code that asks it (0x100a05a50 asserts a
// non-NULL global): every slot returns 0 (slot 61, "Steam cloud on", makes the
// profile loader use the local file), except slot 130, the extension.
#define CS_SLOTS 147
typedef struct {
  void **vtbl;
  void *ext;
} fake_connect;
static void *cs_vtbl[CS_SLOTS];
static fake_connect fake_cs = {cs_vtbl, NULL};
static uint64_t cs_zero(void) { return 0; }
static void *cs_extension(void *self) { return ((fake_connect *)self)->ext; }
static void fake_connect_install(void) {
  for (int i = 0; i < CS_SLOTS; i++) cs_vtbl[i] = (void *)cs_zero;
  cs_vtbl[130] = (void *)cs_extension;
  *RS_FN(void **, RS_ORIG_CONNECT_SERVICE) = &fake_cs;
}

// Profile load 0x100168df0(?, path, ctx, callback): through a temporary file.
static struct {
  int fail, code;
  uint8_t *buf;
  uint32_t len;
} pl_res;
static void pl_callback(int fail, int code, int err, void *ctx, uint8_t *buf, uint32_t len) {
  (void)err;
  (void)ctx;
  pl_res.fail = fail;
  pl_res.code = code;
  pl_res.len = 0;
  pl_res.buf = NULL;
  if (buf) {
    pl_res.buf = malloc(len ? len : 1);
    memcpy(pl_res.buf, buf, len);
    pl_res.len = len;
    RS_FN(void (*)(void *, size_t), RS_ORIG_EFD_FREE_SIZED)(buf, (size_t)-1);
  }
}
static void orig_profile_load(const uint8_t *file, uint32_t n, int *fail, int *code, uint8_t **out, uint32_t *len) {
  const char *tmp = getenv("TMPDIR");
  char path[1024];
  snprintf(path, sizeof path, "%s/rs_profile_XXXXXX", tmp && *tmp ? tmp : "/tmp");
  int fd = mkstemp(path);
  if (fd < 0 || write(fd, file, n) != (ssize_t)n) {
    *fail = *code = -2;
    if (fd >= 0) close(fd), unlink(path);
    return;
  }
  close(fd);
  pl_res.fail = pl_res.code = -1;
  RS_FN(void (*)(void *, const char *, void *, void *), RS_ORIG_PROFILE_LOAD)(NULL, path, NULL, (void *)pl_callback);
  unlink(path);
  *fail = pl_res.fail;
  *code = pl_res.code;
  *out = pl_res.buf;
  *len = pl_res.len;
}

// Package-config callback 0x1002a7550(efd::File *): a fake file (slot 5 Read,
// slot 10 GetSize); it returns a memory stream (+0x20 buffer, +0x2c length).
typedef struct {
  efd_fake_hdr h;
  const uint8_t *data;
  uint32_t n;
} fake_file;
static uint32_t ff_read(void *self, void *buf, uint32_t n) {
  fake_file *f = self;
  memcpy(buf, f->data, n < f->n ? n : f->n);
  return n;
}
static uint32_t ff_size(void *self) { return ((fake_file *)self)->n; }
static void *ff_vtbl[EFD_FAKE_SLOTS];
static uint32_t orig_pkgconfig(const uint8_t *file, uint32_t n, uint8_t **out) {
  fake_file f = {{ff_vtbl, "File"}, file, n};
  uint8_t *stream = RS_FN(uint8_t * (*)(void *), RS_ORIG_PKGCONFIG_DECRYPT)(&f);
  uint8_t *buf = *(uint8_t **)(stream + 0x20);
  uint32_t t = *(uint32_t *)(stream + 0x2c);
  *out = malloc(t ? t : 1);
  memcpy(*out, buf, t);
  RS_FN(void (*)(void *, size_t), RS_ORIG_EFD_FREE_SIZED)(buf, (size_t)-1);
  return t;  // the 0x38-byte stream object is left (efd heap, test process)
}

// DLC preview metadata 0x100a2aeb0: its block loop, on the original Rijndael.
static void orig_preview_ecb(uint8_t *b, size_t n) {
  uint32_t rk[60];
  int nr = RS_FN(int (*)(uint32_t *, const uint8_t *, int), RS_ORIG_RIJNDAEL_KEY_SETUP_DEC)(rk, ck.preview, 256);
  for (size_t i = 0; i + 16 <= n; i += 16) {
    uint8_t t[16];
    memcpy(t, b + i, 16);
    RS_FN(void (*)(const uint32_t *, int, const uint8_t *, uint8_t *), RS_ORIG_RIJNDAEL_DECRYPT)(rk, nr, t, b + i);
  }
  memset(rk, 0, sizeof rk);
}

#define ORIG_CR(field, addr) .field = RS_FN(__typeof__(((crypto_api *)0)->field), addr)
static crypto_api orig_crypto_api(void) {
  crypto_api a = {
      ORIG_CR(ks_enc, RS_ORIG_RIJNDAEL_KEY_SETUP_ENC),
      ORIG_CR(ks_dec, RS_ORIG_RIJNDAEL_KEY_SETUP_DEC),
      ORIG_CR(enc, RS_ORIG_RIJNDAEL_ENCRYPT),
      ORIG_CR(dec, RS_ORIG_RIJNDAEL_DECRYPT),
      ORIG_CR(bpe, RS_ORIG_BPE_EXPAND),
      ORIG_CR(ctr, RS_ORIG_CRYPTOPP_AES_CTR),
      ORIG_CR(cfb, RS_ORIG_CRYPTOPP_AES_CFB),
      ORIG_CR(dsa, RS_ORIG_CRYPTOPP_DSA_VERIFY),
      ORIG_CR(profile_encrypt, RS_ORIG_PROFILE_ENCRYPT),
      .profile_load = orig_profile_load,
      .pkgconfig = orig_pkgconfig,
      .preview_ecb = orig_preview_ecb,
  };
  return a;
}

// Original crypto vs the rewrites. Writes "K group case lines crc", after a
// "KR psarcs sngs pkgconfig" line naming the real-data inputs found.
static void crypto_difftest(FILE *golden, const char *game_dir, int unwind) {
  CHECK(unwind, "image unwind info not registered: Crypto++ exceptions would terminate");
  int n = rsimage_run_inits(RS_RANGE_LIB_CRYPTOPP);  // already run by the efd test; harmless
  CHECK(n >= 0, "Crypto++ static initializers");
  fake_connect_install();
  efd_fill_unexpected(ff_vtbl);
  ff_vtbl[5] = (void *)ff_read;
  ff_vtbl[10] = (void *)ff_size;

  crypto_keys_load(img_at);
  // The original unpacker must give the same key text (compared here, never printed).
  static const uint64_t blobs[3][2] = {{RS_KEY_SNG_DSA_BLOB_LEN, RS_KEY_SNG_DSA_BLOB},
                                       {RS_KEY_SNG_AES_BLOB_LEN, RS_KEY_SNG_AES_BLOB},
                                       {RS_KEY_PKG_DSA_BLOB_LEN, RS_KEY_PKG_DSA_BLOB}};
  const char *mine[3] = {ck.sng_dsa, ck.sng_aes, ck.pkg_dsa};
  for (int i = 0; i < 3; i++) {
    static uint8_t o[0x800];
    memset(o, 0, sizeof o);
    int r = RS_FN(int (*)(const uint8_t *, int, uint8_t *), RS_ORIG_BPE_EXPAND)(img_at(blobs[i][1]),
                                                                                 *(const int *)img_at(blobs[i][0]), o);
    CHECK(r == ck.blob_len[i] && memcmp(o, mine[i], strlen(mine[i]) + 1) == 0, "key blob %d: original unpacker differs", i);
    memset(o, 0, sizeof o);
  }
  clock_t t0 = clock();
  cr_dsa_keys_init();
  crypto_real_init(game_dir);
  printf("crypto: test DSA keys in %.1f s; game data: %d archives, %d .sng files, pkgconfig %s\n",
         (double)(clock() - t0) / CLOCKS_PER_SEC, cr_npsarcs, cr_nsngs, cr_pkg.psarc >= 0 ? "found" : "absent");
  fprintf(golden, "KR %d %d %d\n", cr_npsarcs, cr_nsngs, cr_pkg.psarc >= 0);

  crypto_api orig = orig_crypto_api();
  char *otx = NULL;
  size_t ocap = 0;
  for (int g = 0; g < CR_GROUPS; g++) {
    int lines = 0, bad = 0, cases = crypto_group_cases(g);
    for (int i = 0; i < cases; i++) {
      if (getenv("RS_CRYPTO_TRACE")) fprintf(stderr, "crypto %s case %d\n", crypto_group_names[g], i);
      crypto_run_case(&orig, g, i);
      if (ocap < efd_tx_len + 1) otx = realloc(otx, ocap = efd_tx_len + 1);
      memcpy(otx, efd_tx, efd_tx_len + 1);
      int olines = efd_tx_lines;
      unsigned long ocrc = crc32(0, (const uint8_t *)otx, (uInt)efd_tx_len);
      crypto_run_case(&crypto_rewrite_api, g, i);
      int same = strcmp(otx, efd_tx) == 0;
      checks += olines - 1;
      lines += olines;
      CHECK(same, "crypto %s case %d differs", crypto_group_names[g], i);
      if (!same && bad++ < 3) print_first_diff(otx, efd_tx);
      fprintf(golden, "K %d %d %d %08lx\n", g, i, olines, ocrc);
      if (getenv("RS_CRYPTO_DUMP") && strcmp(getenv("RS_CRYPTO_DUMP"), crypto_group_names[g]) == 0)
        fprintf(stderr, "%s %d: %s", crypto_group_names[g], i, otx);
    }
    printf("crypto %-10s %5d cases, %6d transcript lines\n", crypto_group_names[g], cases, lines);
  }
  free(otx);
}

// --- the online-layer stub vs the original RSConnectExtension_RDV ---------------
static void *x86_new_empty_object(void *ctx) {  // what 0x100a20570 makes
  (void)ctx;
  void *o = RS_FN(void *(*)(size_t, int), RS_ORIG_EFD_ALLOC)(0x78, 0);
  RS_FN(void (*)(void *), RS_ORIG_JSON_VALUE_CTOR)(o);
  EXT_FN(o, 0, void (*)(void *))(o);
  return o;
}

// A finished login job in the original manager's queue (ext+0x10 -> manager,
// jobs vector at +0x10/+0x18/+0x20): type 4 (login), state 3 (failed).
typedef struct {
  void **vtbl;
  int32_t refs;
  int32_t pad;
  int32_t type, state;
  uint8_t rest[0x80];
} fake_job;
static int job_released;
static void job_destroy(void *self) {
  (void)self;
  job_released++;
}
static void *job_vtbl[4] = {(void *)cs_zero, (void *)job_destroy, (void *)cs_zero, (void *)cs_zero};
static fake_job the_job;
static void *job_vec[1];
static uint32_t job_error = 0x00010001;  // a generic (not 0xb....., not credential) error
static uint8_t job_cred_flag, job_cancelled;
static void orig_login_fail(void *ext) {
  uint8_t *mgr = *(uint8_t **)((uint8_t *)ext + 0x10);
  memset(&the_job, 0, sizeof the_job);
  the_job.vtbl = job_vtbl;
  the_job.refs = 1;
  the_job.type = 4;
  the_job.state = 3;
  memcpy((uint8_t *)&the_job + 0x48, &job_error, 4);
  ((uint8_t *)&the_job)[0x4c] = job_cred_flag;
  ((uint8_t *)&the_job)[0x61] = job_cancelled;
  job_vec[0] = &the_job;
  ((void **)(mgr + 0x10))[0] = job_vec;
  ((void **)(mgr + 0x10))[1] = job_vec + 1;
  ((void **)(mgr + 0x10))[2] = job_vec + 1;
}
static void orig_jobs_clear(void *ext) {
  uint8_t *mgr = *(uint8_t **)((uint8_t *)ext + 0x10);
  memset(mgr + 0x10, 0, 24);
}

// The original one-line callers 0x100a05e10... (RSConnectService -> slot 130 -> slot n).
static void online_thunks(void *ext) {
  efd_tx_reset();
  fake_cs.ext = ext;
  uint64_t v[3] = {0, 0, 0};
  static const struct {
    uint64_t addr;
    int slot;
  } t[] = {{0x100a05e10, 21}, {0x100a05e40, 22}, {0x100a05f10, 23}, {0x100a05f30, 25}, {0x100a05f50, 36}};
  for (size_t i = 0; i < sizeof t / sizeof *t; i++) {  // all void
    RS_FN(void (*)(void *, const void *), t[i].addr)(&fake_cs, v);
    efd_txf("thunk %llx (slot %d): done\n", (unsigned long long)t[i].addr, t[i].slot);
  }
  online_sret a = RS_FN(online_sret(*)(void *), 0x100a05ea0)(&fake_cs);
  online_sret b = RS_FN(online_sret(*)(void *, bool), 0x100a05ed0)(&fake_cs, true);
  efd_txf("thunk 100a05ea0 (slot 28): %s; thunk 100a05ed0 (slot 29): %s\n", a.p ? "object" : "null",
          b.p ? "object" : "null");
  if (a.p) obj_release(a.p);
  fake_cs.ext = NULL;
}

static void online_difftest(FILE *golden) {
  online_fakes_init();
  void **gi_global = RS_FN(void **, RS_ORIG_CONNECT_GAME_INTERFACE);
  *gi_global = &fake_gi;

  void *orig = calloc(1, 0x18);
  RS_FN(void (*)(void *), RS_ORIG_RDV_EXT_CTOR)(orig);
  CHECK(*(void **)orig == RS_FN(void *, RS_ORIG_RDV_EXT_VTABLE), "RDV extension vtable");
  rsg_online_config cfg = {online_game_interface, x86_new_empty_object, NULL};
  void *stub = rsg_online_extension_create(&cfg);

  job_released = 0;
  online_ext_transcript(orig, orig_login_fail);
  orig_jobs_clear(orig);
  CHECK(job_released == 1, "original tick released the login job %d times", job_released);
  char *otx = strdup(efd_tx);
  int olines = efd_tx_lines;
  online_ext_transcript(stub, online_stub_login);
  int same = strcmp(otx, efd_tx) == 0;
  checks += olines - 1;
  CHECK(same, "online stub transcript differs from RSConnectExtension_RDV");
  if (!same) print_first_diff(otx, efd_tx);
  if (getenv("RS_ONLINE_DUMP")) fprintf(stderr, "%s", otx);
  fprintf(golden, "O %d %08lx\n", olines, crc32(0, (const uint8_t *)otx, (uInt)strlen(otx)));
  free(otx);

  // Slot 28's object is the original's type.
  online_sret a = EXT_FN(orig, 28, ext_sret0)(orig), b = EXT_FN(stub, 28, ext_sret0)(stub);
  CHECK(a.p && b.p && *(void **)a.p == *(void **)b.p, "slot 28 object types differ");
  obj_release(a.p);
  obj_release(b.p);

  // Original callers.
  online_thunks(orig);
  otx = strdup(efd_tx);
  int tlines = efd_tx_lines;
  online_thunks(stub);
  checks += tlines - 1;
  CHECK(strcmp(otx, efd_tx) == 0, "online stub through the original thunks differs");
  if (strcmp(otx, efd_tx) != 0) print_first_diff(otx, efd_tx);
  free(otx);

  // The other failed-login flavours of the original (documentation; not compared).
  static const struct {
    uint32_t err;
    uint8_t cred, cancelled;
    const char *what;
  } fl[] = {{0x000b0001, 0, 0, "error 0xb0001"}, {0x105, 1, 0, "credential error 0x105"}, {0x00010001, 0, 1, "cancelled"}};
  for (size_t i = 0; i < sizeof fl / sizeof *fl; i++) {
    job_error = fl[i].err;
    job_cred_flag = fl[i].cred;
    job_cancelled = fl[i].cancelled;
    efd_tx_reset();
    orig_login_fail(orig);
    EXT_FN(orig, 12, ext_void0)(orig);
    orig_jobs_clear(orig);
    printf("online: original, failed login job (%s): %s", fl[i].what, efd_tx_len ? efd_tx : "no event\n");
  }
  job_error = 0x00010001;
  job_cred_flag = job_cancelled = 0;

  online_stub_extra(stub);
  int xlines = efd_tx_lines;
  checks += xlines;
  fprintf(golden, "X %d %08lx\n", efd_tx_lines, crc32(0, (const uint8_t *)efd_tx, (uInt)efd_tx_len));
  if (getenv("RS_ONLINE_DUMP")) fprintf(stderr, "%s", efd_tx);
  CHECK(strstr(efd_tx, "event(LoginFail, Err, extra null)") != NULL, "stub: 3 logins post LoginFail");
  CHECK(EXT_FN(stub, 3, uint32_t (*)(void *))(stub) == 1, "stub reference count");
  EXT_FN(stub, 2, ext_void0)(stub);  // last reference: destroys it, releasing slot 28's object
  *gi_global = NULL;
  printf("online: RSConnectExtension_RDV vs stub: %d transcript lines, %d through the original thunks, %d stub-only\n",
         olines, tlines, xlines);
}

// Streaming inflate through the original, feeding 1 KB of input at a time.
static int orig_inflate_stream(const uint8_t *in, size_t inlen, uint8_t *out, size_t cap,
                               size_t *outlen) {
  z_stream s;
  memset(&s, 0, sizeof s);
  int r = RS_FN(inflate_init_fn, RS_ORIG_ZLIB_INFLATE_INIT)(&s, "1.2.3", (int)sizeof s);
  if (r != Z_OK) return r;
  s.next_out = out;
  s.avail_out = (unsigned)cap;
  size_t off = 0;
  do {
    size_t chunk = inlen - off < 1024 ? inlen - off : 1024;
    s.next_in = (uint8_t *)in + off;
    s.avail_in = (unsigned)chunk;
    r = RS_FN(inflate_fn, RS_ORIG_ZLIB_INFLATE)(&s, Z_NO_FLUSH);
    off += chunk - s.avail_in;
  } while (r == Z_OK && off < inlen);
  *outlen = s.total_out;
  RS_FN(inflate_end_fn, RS_ORIG_ZLIB_INFLATE_END)(&s);
  return r;
}

int main(int argc, char **argv) {
  if (argc != 3 && argc != 4) {
    fprintf(stderr, "usage: %s Rocksmith2014_x64_plain GOLDEN_OUT [GAME_DIR]\n", argv[0]);
    return 2;
  }
  rsimage_info info;
  if (rsimage_load(argv[1], &info) != 0) return 2;
  printf("mapped original image: %d imports bound, %d trapped\n", info.resolved, info.unresolved);
  FILE *golden = fopen(argv[2], "w");
  if (!golden) {
    perror(argv[2]);
    return 2;
  }

  // --- AK::SoundEngine::GetIDFromString --------------------------------------
  ak_hash_fn orig_ak = RS_FN(ak_hash_fn, RS_ORIG_AK_GET_ID_FROM_STRING);
  CHECK(orig_ak(NULL) == rsg_ak_get_id_from_string(NULL), "ak(NULL)");
  char name[300];
  for (int i = 0; i < VEC_NAMES; i++) {
    vec_name(name, 259, i);  // <= 258 chars: the original's defined range
    uint32_t o = orig_ak(name), w = rsg_ak_get_id_from_string(name);
    CHECK(o == w, "ak(\"%s\") original %08x rewrite %08x", name, o, w);
    fprintf(golden, "N %d %08x\n", i, o);
  }

  // --- zlib 1.2.3 vs system libz ---------------------------------------------
  uint8_t *blob = malloc(VEC_BLOB_MAX), *comp = malloc(compressBound(VEC_BLOB_MAX));
  uint8_t *o1 = malloc(VEC_BLOB_MAX + 64), *o2 = malloc(VEC_BLOB_MAX + 64);
  for (int i = 0; i < VEC_BLOBS; i++) {
    vec_blob c;
    vec_blob_case(i, blob, &c);
    unsigned long oc = RS_FN(sum_fn, RS_ORIG_ZLIB_CRC32)(0, blob, (unsigned)c.n);
    unsigned long oa = RS_FN(sum_fn, RS_ORIG_ZLIB_ADLER32)(1, blob, (unsigned)c.n);
    CHECK(oc == crc32(0, blob, (uInt)c.n), "crc32 case %d", i);
    CHECK(oa == adler32(1, blob, (uInt)c.n), "adler32 case %d", i);

    uLongf clen = compressBound(VEC_BLOB_MAX);
    compress2(comp, &clen, blob, c.n, c.level);
    if (c.corrupt && clen) comp[c.pos % clen] ^= 0x5a;

    unsigned long l1 = VEC_BLOB_MAX + 64;
    uLongf l2 = VEC_BLOB_MAX + 64;
    int r1 = RS_FN(uncompress_fn, RS_ORIG_ZLIB_UNCOMPRESS)(o1, &l1, comp, clen);
    int r2 = uncompress(o2, &l2, comp, clen);
    CHECK(r1 == r2, "uncompress case %d: original %d libz %d", i, r1, r2);
    if (r1 == Z_OK && r2 == Z_OK) {
      CHECK(l1 == l2 && memcmp(o1, o2, l1) == 0, "uncompress case %d: output differs", i);
      CHECK(l1 == c.n && memcmp(o1, blob, c.n) == 0, "uncompress case %d: not round trip", i);
    }
    size_t l3 = 0;
    int r3 = orig_inflate_stream(comp, clen, o1, VEC_BLOB_MAX + 64, &l3);
    if (!c.corrupt) CHECK(r3 == Z_STREAM_END && l3 == c.n && memcmp(o1, blob, c.n) == 0,
                          "streaming inflate case %d: ret %d len %zu", i, r3, l3);
    fprintf(golden, "C %d %08lx %08lx %d %08lx\n", i, oc, oa, r1,
            r1 == Z_OK ? crc32(0, o2, (uInt)l2) : 0ul);
  }

  // --- Lua 5.1.4 + LNUM vs third_party/lua-5.1.4-lnum ------------------------
  lua_api orig = orig_lua_api();
  int trace = getenv("RS_LUA_TRACE") != NULL;  // print each case before running it
  char src[2048];
  for (int i = 0; i < LUA_CASES; i++) {
    const char *code = src;
    if (i < LUA_FIXED_CASES) code = lua_fixed_cases[i];
    else lua_gen_case(src, sizeof src, i - LUA_FIXED_CASES);
    if (trace) fprintf(stderr, "lua case %d: %s\n", i, code);
    lua_outcome o, w;
    lua_run_case(&orig, code, &o);
    lua_run_case(&lua_rewrite_api, code, &w);
    CHECK(o.load == w.load && o.run == w.run, "lua case %d: status %d/%d vs rewrite %d/%d\n  %s", i,
          o.load, o.run, w.load, w.run, code);
    CHECK(o.result_len == w.result_len && memcmp(o.result, w.result, o.result_len) == 0,
          "lua case %d: %s\n  original: %s\n  rewrite:  %s", i, code, o.result, w.result);
    CHECK(o.dump_len == w.dump_len && memcmp(o.dump, w.dump, o.dump_len) == 0,
          "lua case %d: bytecode differs (%zu vs %zu bytes)\n  %s", i, o.dump_len, w.dump_len, code);
    fprintf(golden, "L %d %d %d %08lx %zu %08lx %zu\n", i, o.load, o.run,
            crc32(0, (const uint8_t *)o.result, (uInt)o.result_len), o.result_len,
            crc32(0, (const uint8_t *)o.dump, (uInt)o.dump_len), o.dump_len);
    lua_free_outcome(&o);
    lua_free_outcome(&w);
  }

  // --- libpng 1.5.13 vs third_party/libpng-1.5.13 ----------------------------
  png_api porig = orig_png_api();
  for (int i = 0; i < PNG_CASES; i++) {
    uint8_t *file;
    size_t n = png_gen_case(i, &file);
    for (int mode = 0; mode < 2; mode++) {
      png_outcome o, w;
      png_decode(&porig, file, n, !mode, &o);
      png_decode(&png_rewrite_api, file, n, !mode, &w);
      CHECK(png_same(&o, &w),
            "png case %d mode %d: original stage %d %ux%u d%d t%d -> d%d t%d rb %zu crc %08x [%s]\n"
            "                        rewrite  stage %d %ux%u d%d t%d -> d%d t%d rb %zu crc %08x [%s]",
            i, mode, o.stage, o.w, o.h, o.depth, o.type, o.depth2, o.type2, o.rowbytes, o.crc, o.msgs,
            w.stage, w.w, w.h, w.depth, w.type, w.depth2, w.type2, w.rowbytes, w.crc, w.msgs);
      fprintf(golden, "P %d %d %08lx %d %u %u %d %d %d %u %u %d %d %zu %08x %08lx\n", i, mode,
              crc32(0, file, (uInt)n), o.stage, o.w, o.h, o.depth, o.type, o.interlace, o.w2, o.h2,
              o.depth2, o.type2, o.rowbytes, o.crc, crc32(0, (const uint8_t *)o.msgs, (uInt)strlen(o.msgs)));
    }
    free(file);
  }

  // --- libjpeg 8d vs third_party/jpeg-8d -------------------------------------
  jpeg_api jorig = orig_jpeg_api();
  for (int i = 0; i < JPEG_FILES; i++) {
    uint8_t *file;
    size_t n = jpeg_gen_case(i, &file);
    int scale = (int)(1 + vec_next() % 16);
    for (int mode = 0; mode < JPEG_MODES; mode++) {
      jpeg_outcome o, w;
      jpeg_decode(&jorig, file, n, mode, scale, &o);
      jpeg_decode(&jpeg_rewrite_api, file, n, mode, scale, &w);
      CHECK(memcmp(&o, &w, offsetof(jpeg_outcome, msgs)) == 0 && strcmp(o.msgs, w.msgs) == 0,
            "jpeg file %d mode %d: original stage %d %ux%u c%d -> %ux%u c%d crc %08x [%s]\n"
            "                          rewrite  stage %d %ux%u c%d -> %ux%u c%d crc %08x [%s]",
            i, mode, o.stage, o.w, o.h, o.comps, o.ow, o.oh, o.ocomps, o.crc, o.msgs, w.stage, w.w,
            w.h, w.comps, w.ow, w.oh, w.ocomps, w.crc, w.msgs);
      fprintf(golden, "J %d %d %08lx %d %d %u %u %u %u %d %d %d %d %d %d %d %d %08x %08lx\n", i, mode,
              crc32(0, file, (uInt)n), o.stage, o.header, o.w, o.h, o.ow, o.oh, o.comps, o.jcs,
              o.progressive, o.arith, o.jfif, o.adobe, o.ocomps, o.ocs, o.crc,
              crc32(0, (const uint8_t *)o.msgs, (uInt)strlen(o.msgs)));
    }
    free(file);
  }
  efd_difftest(golden);
  crypto_difftest(golden, argc > 3 ? argv[3] : NULL, info.unwind);
  online_difftest(golden);
  fclose(golden);
  printf("%d checks, %d passed, %d failed\n", checks, checks - failures, failures);
  return failures ? 1 : 0;
}
