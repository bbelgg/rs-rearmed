// Small efd (Gamebryo LightSpeed Foundation) helpers: UTF-8 utilities of
// efd::utf8string, path standardization, Snprintf, the fast-sqrt table and the
// class-info walk behind IBase::QueryInterface.
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "efd_internal.h"

// Sequence length by lead byte, the table at 0x10142ce80: continuation bytes
// and 0xfe/0xff count as 1, 0xf8-0xfb as 5 and 0xfc-0xfd as 6 (pre-2003 UTF-8).
static int units_of(uint8_t b) {
  if (b < 0xc0) return 1;
  if (b < 0xe0) return 2;
  if (b < 0xf0) return 3;
  if (b < 0xf8) return 4;
  if (b < 0xfc) return 5;
  if (b < 0xfe) return 6;
  return 1;
}

int rsg_efd_utf8_units_at(const char *lead) { return units_of((uint8_t)*lead); }
int rsg_efd_utf8_units(uint64_t lead) { return units_of((uint8_t)lead); }  // lead < 256

static int is_cont(uint8_t b) { return (b & 0xc0) == 0x80; }

// 0x101303850. How many bytes the last sequence of s[0..n) is missing: 0 when
// it is complete and well formed (or n == 0), otherwise `expected - present`
// (which is <= 0 for malformed sequences). Overlong leads are not rejected:
// the original's check for 2-byte leads compares the signed byte with -0x7e
// (0x82), which every 0xc0-0xdf lead passes. Like the original it scans back
// over continuation bytes without a lower bound and checks the sequence's
// bytes even past s + n (the caller's buffer is NUL-terminated).
int rsg_efd_utf8_tail_missing(const char *s, uint32_t n) {
  if (!n) return 0;
  const uint8_t *p = (const uint8_t *)s + n - 1;
  int have = 1;
  while (is_cont(*p)) {
    p--;
    have++;
  }
  uint8_t lead = *p;
  int units = units_of(lead), ok;
  if (units == 1) {
    ok = lead < 0x80;
  } else if (units > 4) {
    ok = 0;
  } else {
    ok = 1;
    for (int i = 1; i < units && ok; i++) ok = is_cont(p[i]);
  }
  return ok ? 0 : units - have;
}

static char *u8_begin(const rsg_efd_utf8string *s) {
  return s->cap == (char *)&s->end ? (char *)s->u.sso : s->u.heap;
}

// 0x1013050e0: number of code points (steps of units_of from begin to end).
size_t rsg_efd_utf8string_length(const rsg_efd_utf8string *s) {
  size_t n = 0;
  for (const char *p = u8_begin(s); p != s->end; p += units_of((uint8_t)*p)) n++;
  return n;
}

// 0x1013064d0 / 0x101306460: libc tolower/toupper on each byte (as a signed
// char, so only ASCII changes in the C locale) up to the first NUL.
static rsg_efd_utf8string *map_case(rsg_efd_utf8string *s, int (*fn)(int)) {
  char *b = u8_begin(s);
  size_t n = (size_t)(s->end - b) + 1;
  for (size_t i = 0; i < n && b[i]; i++) b[i] = (char)fn((signed char)b[i]);
  return s;
}

rsg_efd_utf8string *rsg_efd_utf8string_to_lower(rsg_efd_utf8string *s) { return map_case(s, tolower); }
rsg_efd_utf8string *rsg_efd_utf8string_to_upper(rsg_efd_utf8string *s) { return map_case(s, toupper); }

// 0x1012f6d60, used on file paths by the game's directory setup (0x1002a5ea0)
// and Scaleform's file opener. In place: '\\' becomes '/', a '/' following a
// '/' is dropped unless it is within the first two characters (UNC "//host"),
// then the whole path is lowercased. Returns whether a separator changed.
bool rsg_efd_path_standardize(char *path) {
  if (!path) return false;
  size_t len = strlen(path), w = 0;
  bool changed = false, prev_slash = false;
  for (size_t i = 0; i < len; i++) {
    char c = path[i];
    if (c == '\\') {
      changed = true;
      c = '/';
    }
    if (c == '/' && prev_slash && i > 1) {
      changed = true;
      continue;
    }
    prev_slash = c == '/';
    path[w++] = c;
  }
  path[w] = 0;
  for (char *p = path; *p; p++) *p = (char)tolower((signed char)*p);
  return changed;
}

// 0x10130b200: vsnprintf that reports truncation as -1 and leaves "" when
// count is 0.
int rsg_efd_vsnprintf(char *buf, size_t count, const char *fmt, va_list ap) {
  if (!count) return 0;
  buf[0] = 0;
  int r = vsnprintf(buf, count, fmt, ap);
  if (r >= 0 && (size_t)r >= count) return -1;
  return r;
}

int rsg_efd_snprintf(char *buf, size_t count, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = rsg_efd_vsnprintf(buf, count, fmt, ap);
  va_end(ap);
  return r;
}

// 0x1012f8d60: 256 mantissas, sqrt(1.m) for the 128 7-bit mantissa prefixes m
// followed by sqrt(2 * 1.m). Game code combines one entry with a halved
// exponent for a fast square root.
void rsg_efd_fill_sqrt_table(uint32_t table[256]) {
  for (uint32_t i = 0; i < 128; i++) {
    union {
      uint32_t u;
      float f;
    } a = {0x3f800000u | i << 16}, b = {0x40000000u | i << 16};
    a.f = __builtin_sqrtf(a.f);
    b.f = __builtin_sqrtf(b.f);
    table[i] = a.u & 0x7fffff;
    table[128 + i] = b.u & 0x7fffff;
  }
}

// IBase::QueryInterface (slot 4; slot 5 is the const twin), instantiated per
// class, e.g. MessageService 0x1012f4cb0: walk the class's table for the ID
// (first match wins; multiply-inherited classes list IBase more than once) and
// return the object adjusted by the entry's offset, else NULL. The copy in a
// secondary vtable (e.g. 0x100798dc0, UIMenu_ShopPacks) first subtracts its
// offset-to-top from `this`; the caller passes the complete object here.
bool rsg_efd_query_interface(const void *class_desc, void *self, uint32_t id, void **out) {
  const uint8_t *d = (const uint8_t *)class_desc;
  uint32_t count = *(const uint32_t *)d;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *e = d + 8 + 0x20 * i;
    if (*(const uint32_t *)e == id) {
      *out = (uint8_t *)self + *(const uint32_t *)(e + 0x18);
      return true;
    }
  }
  *out = NULL;
  return false;
}

// IBase::GetClassID (0x10001cb60, shared by all efd classes): the first
// entry's ID.
uint32_t rsg_efd_class_desc_id(const void *class_desc) {
  return *(const uint32_t *)((const uint8_t *)class_desc + 8);
}
