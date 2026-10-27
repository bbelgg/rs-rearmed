// Recompiler runtime: ObjC calls whose ABI differs between x86-64 and arm64.
//
// x86-64 returns a struct larger than 16 bytes through memory: the caller passes
// the buffer in rdi and calls objc_msgSend_stret(buf, self, sel, ...). arm64 has
// no _stret entry point: objc_msgSend returns a "homogeneous float aggregate"
// (up to four floats or doubles, e.g. CGRect) in d0-d3 and any other large
// struct through the buffer in x8. Which one applies depends on the struct
// type, which we read from the method's type encoding.
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdlib.h>
#include <string.h>

#include "rc_call.h"

// Walk one type encoding starting at p: sums size information into st.
typedef struct { int nleaf; char kind; int homogeneous; unsigned size; } leaf_info;

static const char *skip_digits(const char *p) {
  while (*p >= '0' && *p <= '9') p++;
  return p;
}
static const char *walk(const char *p, leaf_info *li) {
  while (*p == 'r' || *p == 'n' || *p == 'N' || *p == 'o' || *p == 'O' || *p == 'R' || *p == 'V') p++;
  switch (*p) {
    case '{': {
      p++;
      while (*p && *p != '=' && *p != '}') p++;
      if (*p == '=') p++;
      while (*p && *p != '}') p = walk(p, li);
      return *p ? p + 1 : p;
    }
    case '[': {
      p++;
      int n = 0;
      while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
      leaf_info sub = {0, 0, 1, 0};
      p = walk(p, &sub);
      if (*p == ']') p++;
      for (int k = 0; k < n; k++) {
        li->nleaf += sub.nleaf;
        if (sub.nleaf && li->kind && sub.kind != li->kind) li->homogeneous = 0;
        if (!li->kind) li->kind = sub.kind;
        if (!sub.homogeneous) li->homogeneous = 0;
      }
      return p;
    }
    case 'f': case 'd':
      li->nleaf++;
      if (li->kind && li->kind != *p) li->homogeneous = 0;
      if (!li->kind) li->kind = *p;
      return skip_digits(p + 1);
    case 0: return p;
    default:  // any other scalar, pointer, object, union...: not floating point
      li->nleaf++;
      li->homogeneous = 0;
      return skip_digits(p + 1);
  }
}

// Is the return type of the method an HFA? Returns the element count (1..4)
// and sets *dbl, or 0.
static int hfa_of(const char *enc, int *dbl) {
  if (!enc) return 0;
  leaf_info li = {0, 0, 1, 0};
  walk(enc, &li);
  if (!li.homogeneous || li.nleaf < 1 || li.nleaf > 4 || !li.kind) return 0;
  *dbl = li.kind == 'd';
  return li.nleaf;
}

void rc_objc_msgsend_stret(rc_ctx *c) {
  rc_args a;
  rc_args_init(&a, c);
  uint64_t sret = rc_arg_i(&a), self = rc_arg_i(&a), sel = rc_arg_i(&a);
  // integer arguments after the selector: rcx r8 r9 then stack (x86) -> x2.. then stack words (arm64 x2..x7)
  uint64_t ix[8] = {self, sel, 0, 0, 0, 0, 0, 0};
  for (int k = 2; k < 8; k++) ix[k] = rc_arg_i(&a);
  uint64_t dx[8], ret[6];
  for (int k = 0; k < 8; k++) dx[k] = c->x[k][0];
  uint64_t fn = (uint64_t)(uintptr_t)&objc_msgSend;
  Class cls = object_getClass((id)(uintptr_t)self);
  Method m = cls ? class_getInstanceMethod(cls, (SEL)(uintptr_t)sel) : 0;
  char enc[256] = "";
  if (m) {
    char *r = method_copyReturnType(m);
    if (r) { strlcpy(enc, r, sizeof enc); free(r); }
  }
  int dbl = 0, n = hfa_of(enc, &dbl);
  if (n) {
    rc_native_call_hfa(fn, ix, dx, ret);
    if (dbl) memcpy((void *)(uintptr_t)sret, &ret[2], 8 * (size_t)n);  // d0..d3 are consecutive in ret
    else {
      float f[4];
      for (int k = 0; k < n; k++) memcpy(&f[k], &ret[2 + k], 4);
      memcpy((void *)(uintptr_t)sret, f, 4 * (size_t)n);
    }
  } else {
    rc_native_call_x8(fn, ix, dx, ret, sret);
  }
  c->r[0] = sret;
}
