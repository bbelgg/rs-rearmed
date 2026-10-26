// efd::FixedString and efd::GlobalStringTable (Gamebryo LightSpeed efd).
//
// A FixedString is a single pointer to interned, reference-counted character
// data. Equal strings share one copy, so comparing two FixedStrings is a
// pointer compare. The 16 bytes in front of the characters are
//   +0  int32 reference count (holders + 1: the table keeps one reference)
//   +8  uint64 (bucket << 16) | strlen   (GetLength reads only the low 16 bits)
// The original allocates them with the efd allocator, rounded up to 8 bytes.
//
// The table (one global, 0x101939c70, created by efd::Init through
// GlobalStringTable::Create 0x1012e3ce0) is 513 buckets, each a vector of
// handles with initial capacity 10 that doubles when full, and a recursive
// mutex. The bucket is `h = h * 33 + (signed char)c` over the bytes, mod 513.
// Two counters (globals 0x101939c78, 0x101939c80) count the strings and the
// inserts into a non-empty bucket.
//
//   AddString    0x1012e3ef0  find by (pointer or length + strcmp); found: ++ref;
//                             else allocate with ref 2 and insert
//   FindString   0x1012e4090
//   InsertString 0x1012e4220
//   RemoveString 0x1012e4380  only if ref == 1: drop it to 0, free, erase
//
// The original also stores the owning pthread and a lock depth next to the
// mutex (+0x3058, +0x3060); nothing reads them, so they are not reproduced.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "efd_internal.h"

namespace {

const uint32_t kBuckets = 513;

struct Bucket {
  const char **begin, **end, **cap;
};

struct Table {
  Bucket bucket[kBuckets];
  pthread_mutex_t lock;
};

Table *g_table;
uint64_t g_count, g_collisions;

struct Locked {
  Locked() { pthread_mutex_lock(&g_table->lock); }
  ~Locked() { pthread_mutex_unlock(&g_table->lock); }
};

int32_t *ref_of(const char *h) { return (int32_t *)(h - 16); }
uint64_t info_of(const char *h) { return *(const uint64_t *)(h - 8); }

uint32_t bucket_of(const char *s) {
  uint32_t h = 0;
  for (; *s; s++) h = h * 33 + (uint32_t)(int32_t)(signed char)*s;
  return h % kBuckets;
}

const char *find(const char *s, size_t len) {
  Locked l;
  Bucket &b = g_table->bucket[bucket_of(s)];
  for (const char **p = b.begin; p != b.end; p++) {
    if (*p == s) return s;
    if (*p && (info_of(*p) & 0xffff) == len && strcmp(*p, s) == 0) return *p;
  }
  return NULL;
}

void insert(const char *h, uint32_t bucket) {
  Locked l;
  Bucket &b = g_table->bucket[bucket];
  if (b.end != b.begin) g_collisions++;
  if (b.end == b.cap) {
    size_t n = (size_t)(b.end - b.begin), cap = n ? 2 * n : 1;
    const char **p = (const char **)malloc(cap * sizeof *p);
    if (n) memcpy(p, b.begin, n * sizeof *p);
    free(b.begin);
    b.begin = p;
    b.end = p + n;
    b.cap = p + cap;
  }
  *b.end++ = h;
  g_count++;
}

void remove(const char *h, uint32_t bucket) {
  Locked l;
  Bucket &b = g_table->bucket[bucket];
  for (const char **p = b.begin; p != b.end; p++) {
    if (*p != h) continue;
    if (h && *ref_of(h) == 1 && __atomic_sub_fetch(ref_of(h), 1, __ATOMIC_SEQ_CST) == 0) {
      free((void *)(h - 16));
      memmove(p, p + 1, (size_t)(b.end - p - 1) * sizeof *p);
      b.end--;
      g_count--;
    }
    return;
  }
}

const char *add(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s);
  Locked l;
  const char *h = find(s, len);
  if (h) {
    __atomic_add_fetch(ref_of(h), 1, __ATOMIC_SEQ_CST);
    return h;
  }
  uint32_t bucket = bucket_of(s);
  uint64_t *hdr = (uint64_t *)malloc((len + 0x11 + 7) & ~(size_t)7);
  hdr[0] = 2;
  hdr[1] = (uint64_t)(uint32_t)(bucket << 16) | len;
  memcpy(hdr + 2, s, len + 1);
  h = (const char *)(hdr + 2);
  insert(h, bucket);
  return h;
}

// The destructor's half of a release: the table's reference is the last one.
void release(const char *h) {
  if (!h) return;
  uint32_t bucket = (uint32_t)info_of(h) >> 16;
  if (__atomic_sub_fetch(ref_of(h), 1, __ATOMIC_SEQ_CST) == 1) remove(h, bucket);
}

}  // namespace

extern "C" {

void rsg_efd_string_table_create(void) {
  Table *t = (Table *)calloc(1, sizeof *t);
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&t->lock, &a);
  pthread_mutexattr_destroy(&a);
  for (uint32_t i = 0; i < kBuckets; i++) {
    Bucket &b = t->bucket[i];
    b.begin = b.end = (const char **)malloc(10 * sizeof *b.begin);
    b.cap = b.begin + 10;
  }
  g_table = t;
}

void rsg_efd_string_table_destroy(void) {
  Table *t = g_table;
  if (!t) return;
  pthread_mutex_destroy(&t->lock);
  for (uint32_t i = 0; i < kBuckets; i++) free(t->bucket[i].begin);
  free(t);
  g_table = NULL;
}

void rsg_efd_fixedstring_init(rsg_efd_fixedstring *s) { s->handle = NULL; }

void rsg_efd_fixedstring_init_cstr(rsg_efd_fixedstring *s, const char *str) { s->handle = add(str); }

void rsg_efd_fixedstring_init_copy(rsg_efd_fixedstring *s, const rsg_efd_fixedstring *o) {
  if (o->handle) __atomic_add_fetch(ref_of(o->handle), 1, __ATOMIC_SEQ_CST);
  s->handle = o->handle;
}

void rsg_efd_fixedstring_release(rsg_efd_fixedstring *s) { release(s->handle); }

rsg_efd_fixedstring *rsg_efd_fixedstring_assign(rsg_efd_fixedstring *s, const rsg_efd_fixedstring *o) {
  const char *h = o->handle;
  if (s->handle == h) return s;
  if (h) __atomic_add_fetch(ref_of(h), 1, __ATOMIC_SEQ_CST);
  release(s->handle);
  s->handle = h;
  return s;
}

const char *rsg_efd_fixedstring_c_str(const rsg_efd_fixedstring *s) { return s->handle; }

bool rsg_efd_fixedstring_exists(const rsg_efd_fixedstring *s) { return s->handle != NULL; }

uint32_t rsg_efd_fixedstring_length(const rsg_efd_fixedstring *s) {
  return s->handle ? (uint32_t)(info_of(s->handle) & 0xffff) : 0;
}

bool rsg_efd_fixedstring_equals(const rsg_efd_fixedstring *s, const char *str) {
  if (s->handle == str) return true;
  if (!str || !s->handle) return false;
  return strcmp(s->handle, str) == 0;
}

uint64_t rsg_efd_string_table_count(void) { return g_count; }
uint64_t rsg_efd_string_table_collisions(void) { return g_collisions; }

uint32_t rsg_efd_string_table_bucket_size(uint32_t bucket) {
  const Bucket &b = g_table->bucket[bucket];
  return (uint32_t)(b.end - b.begin);
}

}  // extern "C"
