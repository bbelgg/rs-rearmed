// efd differential test cases: generators and drivers shared by
// harness/difftest.c (original x86_64 efd code vs rewrites, under Rosetta) and
// test/native_test.c (arm64 rewrites vs the golden file).
//
// A driver calls efd functions through an efd_api table, filled with the
// original's addresses (orig_efd_api() in difftest.c) or the rewrites
// (test/efd_rewrite.h). The entries have the originals' signatures. Everything
// observable goes into a text transcript (efd_fakes.h): return values, object
// fields, buffer contents, reference counts, log messages and every call into
// the fake ServiceManager / MessageService / Logger. The original's transcript
// must equal the rewrite's; the golden file keeps its CRC per case.
//
// Each case seeds its own RNG from (group, index), so cases are independent of
// each other and of the other tests.
#ifndef RS_GAME_EFD_CASES_H
#define RS_GAME_EFD_CASES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "efd_fakes.h"
#include "rs_efd.h"

typedef struct {
  // UniversalID / Category
  bool (*id_set_type)(uint64_t *, int);
  bool (*id_set_usage)(uint64_t *, int);
  bool (*id_set_shard)(uint64_t *, uint32_t);
  bool (*id_set_net_id)(uint64_t *, uint32_t);
  bool (*id_set_base_id)(uint64_t *, uint64_t);
  uint64_t *(*cat_u0)(uint64_t *, uint32_t, uint32_t);
  uint64_t *(*cat_u2)(uint64_t *, uint32_t, uint32_t);
  uint64_t *(*cat_svc)(uint64_t *, void *, uint32_t);
  // FixedString; the st_* entries read the table's state
  void (*fs_init)(rsg_efd_fixedstring *);
  void (*fs_init_cstr)(rsg_efd_fixedstring *, const char *);
  void (*fs_init_copy)(rsg_efd_fixedstring *, const rsg_efd_fixedstring *);
  void (*fs_release)(rsg_efd_fixedstring *);
  rsg_efd_fixedstring *(*fs_assign)(rsg_efd_fixedstring *, const rsg_efd_fixedstring *);
  const char *(*fs_c_str)(const rsg_efd_fixedstring *);
  bool (*fs_exists)(const rsg_efd_fixedstring *);
  uint32_t (*fs_length)(const rsg_efd_fixedstring *);
  bool (*fs_equals)(const rsg_efd_fixedstring *, const char *);
  uint64_t (*st_count)(void);
  uint64_t (*st_collisions)(void);
  uint32_t (*st_bucket_size)(uint32_t);
  // DataStream
  void (*ds_init)(void *, uint32_t);
  void (*ds_init_copy)(void *, const void *, bool);
  void (*ds_destroy)(void *);
  void (*ds_read)(void *, void *, size_t);
  void (*ds_write)(void *, const void *, size_t);
  bool (*ds_seek)(void *, size_t);
  void (*ds_write_stream)(void *, void *);
  void (*ds_read_stream)(void *, void *);
  void *(*ds_write_id)(void *, const uint64_t *);
  void *(*ds_read_id)(void *, uint64_t *);
  // UTF-8 and strings
  int (*utf8_units_at)(const char *);
  int (*utf8_units)(uint64_t);
  int (*utf8_tail_missing)(const char *, uint32_t);
  size_t (*u8_length)(const rsg_efd_utf8string *);
  rsg_efd_utf8string *(*u8_lower)(rsg_efd_utf8string *);
  rsg_efd_utf8string *(*u8_upper)(rsg_efd_utf8string *);
  bool (*path_standardize)(char *);
  int (*efd_snprintf)(char *, size_t, const char *, ...);
  void (*sqrt_table)(uint32_t *);
  // IMessageHelperSystemService
  void (*mh_init)(void *, uint32_t);
  int (*mh_preinit)(void *);
  int (*mh_shutdown)(void *);
  void (*mh_net_id)(void *, const void *);
  int (*mh_on_init)(void *);
  int (*mh_on_tick)(void *);
  const char *(*mh_name)(void *);
} efd_api;

static uint64_t efd_rng;
static void efd_seed(int group, int i) {
  efd_rng = 0x9e3779b97f4a7c15ull ^ ((uint64_t)(group + 1) << 40) ^ (uint64_t)(i + 1) * 0xbf58476d1ce4e5b9ull;
  if (!efd_rng) efd_rng = 1;
}
static uint32_t efd_rand(void) {  // xorshift64*
  efd_rng ^= efd_rng >> 12;
  efd_rng ^= efd_rng << 25;
  efd_rng ^= efd_rng >> 27;
  return (uint32_t)((efd_rng * 0x2545f4914f6cdd1dull) >> 32);
}
static uint64_t efd_rand64(void) { return (uint64_t)efd_rand() << 32 | efd_rand(); }
static uint32_t efd_pick(uint32_t n) { return efd_rand() % n; }

// --- group 0: UniversalID and Category ---------------------------------------
static int efd_small_int(void) {  // mostly in range, sometimes past it or negative
  uint32_t r = efd_pick(10);
  return r < 7 ? (int)efd_pick(4) : r < 8 ? 4 + (int)efd_pick(4) : r < 9 ? -(int)efd_pick(5) : (int)efd_rand();
}

static void efd_case_id(const efd_api *a) {
  uint64_t id;
  switch (efd_pick(4)) {
    case 0: id = efd_rand64(); break;
    case 1: id = efd_rand64() & 0x7fffffffffffffffull; break;
    case 2: id = 0x8000000000000000ull | (uint64_t)efd_pick(4) << 61; break;
    default: id = 0xc000000000000000ull; break;
  }
  efd_txf("start %016llx\n", (unsigned long long)id);
  for (int k = 0; k < 8; k++) {
    int op = (int)efd_pick(8), r = -1;
    uint32_t v = efd_rand(), w = efd_rand();
    uint64_t v64 = efd_rand64();
    if (efd_pick(3) == 0) v &= 0x7ffffff;
    if (efd_pick(4) == 0) v &= 0xfff;
    switch (op) {
      case 0: r = a->id_set_type(&id, efd_small_int()); break;
      case 1: r = a->id_set_usage(&id, efd_small_int()); break;
      case 2: r = a->id_set_shard(&id, efd_pick(3) ? v & 0x3ff : v & 0x7ff); break;
      case 3: r = a->id_set_net_id(&id, v); break;
      case 4: r = a->id_set_base_id(&id, efd_pick(2) ? v64 : v); break;
      case 5: r = a->cat_u0(&id, v, w) == &id; break;
      case 6: r = a->cat_u2(&id, v, w) == &id; break;
      default: {
        efd_fake_ms ms = {{efd_ms_vtbl, "MessageService"}, w, 1, NULL};
        r = a->cat_svc(&id, &ms, v) == &id;
        break;
      }
    }
    efd_txf("op %d -> %d %016llx\n", op, r, (unsigned long long)id);
  }
}

// --- group 1: FixedString / GlobalStringTable --------------------------------
#define EFD_FS_SLOTS 12
#define EFD_FS_POOL 24

static void efd_fs_state(const efd_api *a, const rsg_efd_fixedstring *s, int k, uint64_t c0, uint64_t l0,
                         const uint32_t *b0) {
  const char *h = a->fs_c_str(&s[k]);
  int cls = -1;
  for (int j = 0; h && j < EFD_FS_SLOTS; j++)
    if (s[j].handle == h) {
      cls = j;
      break;
    }
  efd_txf("s%d cls %d ex %d len %u", k, cls, a->fs_exists(&s[k]), a->fs_length(&s[k]));
  if (h) {
    uint64_t info = *(const uint64_t *)(h - 8);
    uint32_t bucket = (uint32_t)info >> 16;
    efd_txf(" rc %d info %llx bsz %+d '%s'", *(const int32_t *)(h - 16), (unsigned long long)info,
            (int)(a->st_bucket_size(bucket) - b0[bucket]), h);
  }
  efd_txf(" n %+lld c %+lld\n", (long long)(a->st_count() - c0), (long long)(a->st_collisions() - l0));
}

static void efd_case_fixedstring(const efd_api *a) {
  static uint32_t b0[513];
  char pool[EFD_FS_POOL][320];
  static const char alpha[] = "abcAB01_.";
  for (int p = 0; p < EFD_FS_POOL; p++) {
    size_t n = efd_pick(8) == 0 ? 40 + efd_pick(270) : efd_pick(6);
    for (size_t k = 0; k < n; k++) pool[p][k] = alpha[efd_pick(sizeof alpha - 1)];
    pool[p][n] = 0;
  }
  uint64_t c0 = a->st_count(), l0 = a->st_collisions();
  for (uint32_t b = 0; b < 513; b++) b0[b] = a->st_bucket_size(b);
  rsg_efd_fixedstring s[EFD_FS_SLOTS];
  for (int k = 0; k < EFD_FS_SLOTS; k++) a->fs_init(&s[k]);
  char copy[320];
  for (int step = 0; step < 80; step++) {
    // creating strings is the most frequent op, so the slots fill up
    int op = efd_pick(3) == 0 ? 0 : (int)efd_pick(8), k = (int)efd_pick(EFD_FS_SLOTS), o = (int)efd_pick(EFD_FS_SLOTS);
    const char *str = pool[efd_pick(EFD_FS_POOL)];
    strcpy(copy, str);  // a different pointer with the same characters
    efd_txf("op %d s%d o%d '%s': ", op, k, o, str);
    switch (op) {
      case 0: a->fs_release(&s[k]); a->fs_init_cstr(&s[k], copy); break;
      case 1: a->fs_release(&s[k]); a->fs_init_cstr(&s[k], NULL); break;
      case 2: {  // the interned pointer itself
        const char *h = a->fs_c_str(&s[o]);
        rsg_efd_fixedstring t;
        a->fs_init_cstr(&t, h);
        efd_txf("same %d ", t.handle == h);
        a->fs_release(&s[k]);
        s[k] = t;
        break;
      }
      case 3: {
        rsg_efd_fixedstring t;
        a->fs_init_copy(&t, &s[o]);
        a->fs_release(&s[k]);
        s[k] = t;
        break;
      }
      case 4: efd_txf("ret %d ", a->fs_assign(&s[k], &s[o]) == &s[k]); break;
      case 5: a->fs_release(&s[k]); a->fs_init(&s[k]); break;
      case 6: efd_txf("eq %d ", a->fs_equals(&s[k], copy)); break;
      default: efd_txf("eq %d ", a->fs_equals(&s[k], a->fs_c_str(&s[o]))); break;
    }
    efd_fs_state(a, s, k, c0, l0, b0);
  }
  for (int k = 0; k < EFD_FS_SLOTS; k++) a->fs_release(&s[k]);
  efd_txf("end n %+lld\n", (long long)(a->st_count() - c0));
}

// --- group 2: DataStream ------------------------------------------------------
typedef struct {
  uint8_t *buf;
  uint32_t cap, size;
  uint8_t *pos;
  int32_t *endian;
  uint8_t ro, owns;
} efd_ds_view;

static efd_ds_view efd_ds(void *p) {
  uint8_t *b = (uint8_t *)p;
  efd_ds_view v = {*(uint8_t **)(b + 0x10), *(uint32_t *)(b + 0x18), *(uint32_t *)(b + 0x1c),
                   *(uint8_t **)(b + 0x20), *(int32_t **)(b + 0x28), b[0x30], b[0x31]};
  return v;
}

static void efd_ds_state(void *p, int k) {
  efd_ds_view v = efd_ds(p);
  efd_txf("  s%d size %u cap %u pos %lld ro %d owns %d little %d refs %d crc %08lx\n", k, v.size, v.cap,
          (long long)(v.pos - v.buf), v.ro, v.owns, v.endian ? v.endian[2] : -1, v.endian ? v.endian[0] : -1,
          crc32(0, v.buf, v.size));
}

static uint32_t efd_ds_capacity(void) {
  static const uint32_t caps[] = {0, 1, 4, 16, 100, 1024};
  return efd_pick(4) ? caps[efd_pick(6)] : efd_pick(5000);
}

static void efd_case_datastream(const efd_api *a) {
  uint64_t mem[3][RSG_EFD_DATASTREAM_SIZE / 8];
  uint8_t data[4096], got[4096];
  memset(mem, 0, sizeof mem);
  for (int k = 0; k < 3; k++) a->ds_init(mem[k], efd_ds_capacity());
  for (int step = 0; step < 40; step++) {
    // writes are the most frequent op, so there is data to read, seek and copy
    int op = efd_pick(3) == 0 ? 0 : efd_pick(4) == 0 ? 3 : (int)efd_pick(11), k = (int)efd_pick(3),
        o = (k + 1 + (int)efd_pick(2)) % 3;
    efd_ds_view v = efd_ds(mem[k]);
    size_t n = efd_pick(3) ? efd_pick(40) : efd_pick(4) ? efd_pick(400) : efd_pick(4096);
    for (size_t i = 0; i < n; i++) data[i] = (uint8_t)efd_rand();
    efd_txf("op %d s%d o%d n %zu", op, k, o, n);
    switch (op) {
      case 0: a->ds_write(mem[k], data, n); break;
      case 1:
        memset(got, 0xcd, n);
        a->ds_read(mem[k], got, n);
        efd_txf(" got %08lx", crc32(0, got, (uInt)n));
        break;
      case 2: {
        size_t off = efd_pick(4) ? efd_pick(v.size + 2) : efd_rand();
        efd_txf(" off %zu -> %d", off, a->ds_seek(mem[k], off));
        break;
      }
      case 3: {
        uint64_t id = efd_rand64();
        efd_txf(" -> %d", a->ds_write_id(mem[k], &id) == (void *)mem[k]);
        break;
      }
      case 4: {
        uint64_t id = 0;
        int whole = v.size - (uint32_t)(v.pos - v.buf) >= 8;
        efd_txf(" -> %d", a->ds_read_id(mem[k], &id) == (void *)mem[k]);
        if (whole) efd_txf(" id %016llx", (unsigned long long)id);  // else undefined in the original
        break;
      }
      case 5: a->ds_write_stream(mem[k], mem[o]); break;
      case 6:
        if (v.size - (uint32_t)(v.pos - v.buf) >= 4) a->ds_read_stream(mem[k], mem[o]);
        break;
      case 7: ((uint8_t *)mem[k])[0x30] = (uint8_t)efd_pick(2); break;
      case 8: v.endian[2] = (int32_t)efd_pick(2); break;
      case 9:
        a->ds_destroy(mem[k]);
        a->ds_init_copy(mem[k], mem[o], efd_pick(2));
        break;
      default:
        a->ds_destroy(mem[k]);
        a->ds_init(mem[k], efd_ds_capacity());
        break;
    }
    efd_txf("\n");
    efd_ds_state(mem[k], k);
    if (op == 5 || op == 6 || op == 9) efd_ds_state(mem[o], o);
  }
  for (int k = 0; k < 3; k++) a->ds_destroy(mem[k]);
}

// --- group 3: UTF-8 -----------------------------------------------------------
static int efd_units_ref(uint8_t b) {  // the table at 0x10142ce80, for the generator
  return b < 0xc0 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : b < 0xf8 ? 4 : b < 0xfc ? 5 : b < 0xfe ? 6 : 1;
}

static size_t efd_gen_utf8(char *s, size_t cap) {
  size_t n = 0, target = efd_pick(4) ? efd_pick(20) : efd_pick(60);
  while (n < target && n + 8 < cap) {
    uint32_t r = efd_pick(10), cp;
    if (r < 4) {  // ASCII
      s[n++] = (char)(efd_pick(2) ? 'A' + efd_pick(58) : 1 + efd_pick(127));
      continue;
    }
    if (r < 8) {  // a well-formed code point
      cp = r == 4 ? 0x80 + efd_pick(0x780) : r == 5 ? 0x800 + efd_pick(0xf800) : r == 6 ? 0x10000 + efd_pick(0x100000) : 0xc0 + efd_pick(0x40);
      if (cp < 0x800) {
        s[n++] = (char)(0xc0 | cp >> 6);
        s[n++] = (char)(0x80 | (cp & 0x3f));
      } else if (cp < 0x10000) {
        s[n++] = (char)(0xe0 | cp >> 12);
        s[n++] = (char)(0x80 | (cp >> 6 & 0x3f));
        s[n++] = (char)(0x80 | (cp & 0x3f));
      } else {
        s[n++] = (char)(0xf0 | cp >> 18);
        s[n++] = (char)(0x80 | (cp >> 12 & 0x3f));
        s[n++] = (char)(0x80 | (cp >> 6 & 0x3f));
        s[n++] = (char)(0x80 | (cp & 0x3f));
      }
      continue;
    }
    // any lead byte followed by the number of nonzero bytes its table entry says
    uint8_t lead = (uint8_t)(0x80 + efd_pick(0x80));
    int units = efd_units_ref(lead);
    s[n++] = (char)lead;
    for (int i = 1; i < units; i++) s[n++] = (char)(efd_pick(3) ? 0x80 + efd_pick(0x40) : 1 + efd_pick(255));
  }
  s[n] = 0;
  return n;
}

static void efd_u8_make(rsg_efd_utf8string *u, const char *s, size_t n, char *heap) {
  if (n < 16) {
    memcpy(u->u.sso, s, n + 1);
    u->end = u->u.sso + n;
    u->cap = (char *)&u->end;
  } else {
    memcpy(heap, s, n + 1);
    u->u.heap = heap;
    u->end = heap + n;
    u->cap = heap + n + 1;
  }
}

static void efd_case_utf8(const efd_api *a) {
  char s[128], guarded[130], heap[128];
  size_t n = efd_gen_utf8(s, 100);
  guarded[0] = 'G';  // a non-continuation byte before the string: the tail scan has no lower bound
  memcpy(guarded + 1, s, n + 1);
  rsg_efd_utf8string u;
  efd_u8_make(&u, s, n, heap);
  efd_txf("n %zu", n);
  for (size_t k = 0; k < n; k++) efd_txf("%s%02x", k ? "" : " ", (uint8_t)s[k]);
  efd_txf(" len %zu tail", a->u8_length(&u));
  for (uint32_t k = 0; k <= n; k++) efd_txf(" %d", a->utf8_tail_missing(guarded + 1, k));
  efd_txf("\n");
  int upper = (int)efd_pick(2);
  rsg_efd_utf8string *r = upper ? a->u8_upper(&u) : a->u8_lower(&u);
  const char *b = n < 16 ? u.u.sso : u.u.heap;
  efd_txf("%s ret %d crc %08lx len %zu\n", upper ? "upper" : "lower", r == &u, crc32(0, (const uint8_t *)b, (uInt)n),
          strlen(b));
}

static void efd_case_units(const efd_api *a) {
  for (int b = 0; b < 256; b++) {
    char c = (char)b;
    efd_txf("%02x %d %d\n", b, a->utf8_units_at(&c), a->utf8_units((uint64_t)b));
  }
}

// --- group 4: paths, Snprintf, sqrt table ------------------------------------
static void efd_case_path(const efd_api *a) {
  static const char alpha[] = "aB/\\\\//.:x Z_9";
  char p[80];
  size_t n = efd_pick(50);
  for (size_t k = 0; k < n; k++) p[k] = efd_pick(20) ? alpha[efd_pick(sizeof alpha - 1)] : (char)(0x80 + efd_pick(0x80));
  p[n] = 0;
  efd_txf("'%s' -> ", p);
  int r = a->path_standardize(p);
  efd_txf("%d '%s'\n", r, p);
}

static void efd_case_snprintf(const efd_api *a) {
  static const double dv[] = {0.0, -0.0, 1.5, -2.25, 3.14159265358979, 1e10, -1e-7, 123456.789, 0.1, 2.5e300};
  char buf[96], str[96];
  size_t sl = efd_pick(4) ? efd_pick(12) : efd_pick(80);
  for (size_t k = 0; k < sl; k++) str[k] = (char)('a' + efd_pick(26));
  str[sl] = 0;
  size_t count = efd_pick(5) ? efd_pick(48) : efd_pick(2);
  int fmt = (int)efd_pick(8), r;
  int iv = (int)efd_rand();
  long long lv = (long long)efd_rand64();
  double d1 = dv[efd_pick(10)], d2 = dv[efd_pick(10)] * (1 + efd_pick(7));
  memset(buf, '#', sizeof buf);
  switch (fmt) {
    case 0: r = a->efd_snprintf(buf, count, "%d|%s", iv, str); break;
    case 1: r = a->efd_snprintf(buf, count, "%5.2f;%x", d1, (unsigned)iv); break;
    case 2: r = a->efd_snprintf(buf, count, "%-10s<%c>", str, 'A' + (iv & 15)); break;
    case 3: r = a->efd_snprintf(buf, count, "%lld%%", lv); break;
    case 4: r = a->efd_snprintf(buf, count, "%e %g", d1, d2); break;
    case 5: r = a->efd_snprintf(buf, count, "%s", str); break;
    case 6: r = a->efd_snprintf(buf, count, "%s", ""); break;
    default: r = a->efd_snprintf(buf, count, "%08.3f%s%d", d2, str, iv); break;
  }
  buf[sizeof buf - 1] = 0;
  efd_txf("fmt %d count %zu -> %d '%s'\n", fmt, count, r, count ? buf : "(untouched)");
  if (!count) efd_txf("untouched %d\n", buf[0] == '#');
}

static void efd_case_sqrt(const efd_api *a) {
  uint32_t t[256];
  a->sqrt_table(t);
  for (int i = 0; i < 256; i++) efd_txf("%06x%s", t[i], i % 16 == 15 ? "\n" : " ");
}

// --- group 5: IMessageHelperSystemService with a fake ServiceManager ------------
static void efd_mh_fields(const void *obj) {
  const uint8_t *b = (const uint8_t *)obj;
  efd_txf("  priority %d class %#x net %#x\n", *(const int32_t *)(b + 0x10), *(const uint32_t *)(b + 0x14),
          *(const uint32_t *)(b + 0x18));
}

static void efd_case_msghelper(const efd_api *a) {
  static const uint32_t ids[] = {0x7001004, 0x1000600, 0x7000003, 0x1006, 0xe0000012};
  uint64_t obj[RSG_EFD_MSGHELPER_SIZE / 8];
  uint8_t msg[0x48];
  uint32_t cid = efd_pick(2) ? ids[efd_pick(5)] : efd_rand();
  int scenario = (int)efd_pick(5);  // 0-2 normal, 3 no service, 4 QueryInterface fails
  efd_fake_ms ms = {{efd_ms_vtbl, "MessageService"}, efd_pick(2) ? efd_pick(16) : efd_rand(), scenario != 4, obj};
  efd_fake_sm sm = {{efd_sm_vtbl, "ServiceManager"}, scenario == 3 ? NULL : &ms};
  memset(obj, 0, sizeof obj);
  efd_txf("class %#x scenario %d\n", cid, scenario);
  a->mh_init(obj, cid);
  ((void **)obj)[1] = &sm;
  efd_mh_fields(obj);
  efd_txf("preinit -> %d\n", a->mh_preinit(obj));
  efd_mh_fields(obj);
  for (int k = 0; k < 3; k++) {
    uint32_t net = efd_pick(3) ? efd_rand() : ((uint32_t *)obj)[6];
    memset(msg, 0, sizeof msg);
    memcpy(msg + 0x3c, &net, 4);
    efd_txf("net id %#x\n", net);
    a->mh_net_id(obj, msg);
    efd_mh_fields(obj);
  }
  efd_txf("init %d tick %d name %s\n", a->mh_on_init(obj), a->mh_on_tick(obj), a->mh_name(obj));
  efd_txf("shutdown -> %d\n", a->mh_shutdown(obj));
  efd_mh_fields(obj);
}

// --- groups ----------------------------------------------------------------------
enum { EFD_G_ID, EFD_G_FIXEDSTRING, EFD_G_DATASTREAM, EFD_G_UTF8, EFD_G_UNITS, EFD_G_PATH, EFD_G_SNPRINTF,
       EFD_G_SQRT, EFD_G_MSGHELPER, EFD_GROUPS };
static const int efd_group_cases[EFD_GROUPS] = {2000, 300, 400, 1500, 1, 1500, 600, 1, 300};
static const char *const efd_group_names[EFD_GROUPS] = {
    "UniversalID/Category", "FixedString", "DataStream", "utf8", "utf8 units", "path", "Snprintf", "sqrt table",
    "IMessageHelperSystemService"};

// Runs one case into a fresh transcript.
static void efd_run_case(const efd_api *a, int group, int i) {
  efd_tx_reset();
  efd_seed(group, i);
  switch (group) {
    case EFD_G_ID: efd_case_id(a); break;
    case EFD_G_FIXEDSTRING: efd_case_fixedstring(a); break;
    case EFD_G_DATASTREAM: efd_case_datastream(a); break;
    case EFD_G_UTF8: efd_case_utf8(a); break;
    case EFD_G_UNITS: efd_case_units(a); break;
    case EFD_G_PATH: efd_case_path(a); break;
    case EFD_G_SNPRINTF: efd_case_snprintf(a); break;
    case EFD_G_SQRT: efd_case_sqrt(a); break;
    default: efd_case_msghelper(a); break;
  }
}

// QueryInterface queries for one class: its own table's IDs, then IDs it
// should not have.
static int efd_qi_queries(const uint32_t *ids, int n, uint32_t *q) {
  static const uint32_t extra[] = {0xffffffffu, 0, 0x7000001, 0x7000003, 0x700000e, 0x7001000, 0x7002000,
                                   0x1000506,   0xe000000b, 0x1006, 0x12345678};
  int m = 0;
  for (int i = 0; i < n; i++) q[m++] = ids[i];
  for (size_t i = 0; i < sizeof extra / sizeof *extra; i++) q[m++] = extra[i];
  for (int i = 0; i < n; i++) q[m++] = ids[i] ^ 1;
  return m;
}

#endif
