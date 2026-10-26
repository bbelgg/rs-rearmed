// Recompiler runtime: native-callable pointers for guest functions
// (pthread start routines, qsort comparators, atexit handlers, callbacks).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "rc_rt.h"

#define POOL 2048
extern const uint8_t rc_tramp_pool[], rc_tramp_pool_end[];

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t fn_of[POOL];     // guest function (host address) of entry k
static uint32_t flags_of[POOL];  // RC_TR_* of entry k
static int used;
#define HSIZE 8192  // open addressing, > 2 * POOL
static struct { uint64_t fn; uint32_t flags; int idx; } hash[HSIZE];

// rc_tramp_get_fixfn: RC_TR_FIXFN | nfields << 8
#define RC_TR_FIXFN 64

static unsigned hslot(uint64_t fn, uint32_t fl) {
  return (unsigned)((((fn >> 2) ^ ((uint64_t)fl << 40)) * 0x9E3779B97F4A7C15ull) >> 51) & (HSIZE - 1);
}

static int pool_index(uint64_t host) {
  uint64_t base = (uint64_t)rc_tramp_pool;
  if (host < base || host >= base + 8ull * POOL || ((host - base) & 7)) return -1;
  return (int)((host - base) >> 3);
}

uint64_t rc_tramp_guest_of(uint64_t host) {
  int k = pool_index(host);
  return k >= 0 && k < used ? fn_of[k] : 0;
}
uint64_t rc_tramp_unwrap(uint64_t host) {
  uint64_t g = rc_tramp_guest_of(host);
  return g ? g : host;
}

void *rc_tramp_get_ex(uint64_t guest_fn, unsigned flags) {
  guest_fn = rc_tramp_unwrap(guest_fn);  // a trampoline already: wrap the function behind it
  if (!rc_in_image(guest_fn)) return (void *)guest_fn;
  if ((size_t)(rc_tramp_pool_end - rc_tramp_pool) != 8 * POOL) rc_fail("trampoline pool layout", 0);
  pthread_mutex_lock(&mu);
  unsigned h = hslot(guest_fn, flags);
  while (hash[h].fn && (hash[h].fn != guest_fn || hash[h].flags != flags)) h = (h + 1) & (HSIZE - 1);
  if (!hash[h].fn) {
    if (used == POOL) {
      pthread_mutex_unlock(&mu);
      rc_fail("trampoline pool exhausted", guest_fn - rc_slide);
    }
    fn_of[used] = guest_fn;
    flags_of[used] = flags;
    hash[h].fn = guest_fn;
    hash[h].flags = flags;
    hash[h].idx = used++;
  }
  void *p = (void *)(rc_tramp_pool + 8 * (size_t)hash[h].idx);
  pthread_mutex_unlock(&mu);
  return p;
}

void *rc_tramp_get(uint64_t guest_fn) { return rc_tramp_get_ex(guest_fn, 0); }

void *rc_tramp_get_fixfn(uint64_t fn, int nfields) {
  return rc_tramp_get_ex(fn, RC_TR_FIXFN | ((unsigned)nfields << 8));
}

void rc_unwrap_ptr_end(uint64_t **pp) {
  if (*pp) **pp = rc_tramp_unwrap(**pp);
}
void rc_unwrap_ret_end(rc_ctx **pc) { (*pc)->r[0] = rc_tramp_unwrap((*pc)->r[0]); }

// Apple's arm64 compilers assume a callee extended a result narrower than 32 bits to 32 bits (they
// use w0 as is); x86 leaves the upper bits of eax undefined.
static uint64_t extend_ret(uint64_t rax, unsigned kind) {
  switch (kind) {
    case RC_TR_RET_U8: return (uint8_t)rax;
    case RC_TR_RET_S8: return (uint32_t)(int32_t)(int8_t)rax;
    case RC_TR_RET_U16: return (uint16_t)rax;
    case RC_TR_RET_S16: return (uint32_t)(int32_t)(int16_t)rax;
    case RC_TR_RET_BOOL: return (rax & 0xff) != 0;
  }
  return rax;
}

// Argument assembly in x86-64 SysV order.
typedef struct {
  uint64_t gi[6], dv[8], stk[80];
  int ng, nd, ns;
} xargs;
static void put_i(xargs *a, uint64_t v) {
  if (a->ng < 6) a->gi[a->ng++] = v;
  else if (a->ns < 80) a->stk[a->ns++] = v;
  else rc_fail("native->guest call with too many arguments", 0);
}
static void put_d(xargs *a, uint64_t v) {
  if (a->nd < 8) a->dv[a->nd++] = v;
  else if (a->ns < 80) a->stk[a->ns++] = v;
  else rc_fail("native->guest call with too many arguments", 0);
}

// The `...` of a printf-style callee called by arm64 native code: one 8-byte stack slot per argument
// (long double is a double there), types given by the conversion specifiers.
static void put_varargs(xargs *a, const char *fmt, const uint64_t *slot) {
  for (const char *p = fmt; *p;) {
    if (*p++ != '%') continue;
    if (*p == '%') { p++; continue; }
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' || *p == '\'') p++;
    if (*p == '*') { put_i(a, *slot++); p++; } else while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
      p++;
      if (*p == '*') { put_i(a, *slot++); p++; } else while (*p >= '0' && *p <= '9') p++;
    }
    int ldbl = 0;
    for (;; p++) {
      if (*p == 'L') ldbl = 1;
      else if (!(*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'q')) break;
    }
    switch (*p) {
      case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
        if (ldbl) rc_fail("native->guest variadic call with a long double", 0);
        put_d(a, *slot++);
        break;
      case 0: continue;
      default: put_i(a, *slot++);
    }
    p++;
  }
}

// Called from rc_tramp_common with the native argument registers saved in frame:
// [0..7] x0..x7, [8..15] d0..d7, [18] x8, [19] the caller's sp (its stack arguments).
// It leaves rax, rdx, xmm0.lo, xmm1.lo in frame[0], frame[1], frame[8], frame[9].
void rc_tramp_dispatch(uint64_t idx, uint64_t *frame) {
  uint64_t fn = fn_of[idx];  // published before the pointer escaped
  unsigned fl = flags_of[idx];
  uint64_t o[4];
  if (!fl) {
    rc_call_guest_ex(fn, frame, 8, frame + 8, 8, o);
  } else {
    xargs a;
    memset(&a, 0, sizeof a);
    if (fl & RC_TR_SRET) put_i(&a, frame[18]);  // x86 passes the result pointer in rdi, x8 on arm64
    int named = (fl & RC_TR_VA) ? (int)((fl >> 8) & 255) + 1 : 8;
    for (int i = 0; i < named && i < 8; i++) put_i(&a, frame[i]);
    if (fl & RC_TR_VA) {
      const char *fmt = (const char *)(uintptr_t)frame[named - 1];
      put_varargs(&a, fmt, (const uint64_t *)(uintptr_t)frame[19]);
    } else {
      for (int i = 0; i < 8; i++) put_d(&a, frame[8 + i]);
    }
    rc_call_guest_regs(fn, a.gi, a.ng, a.dv, a.nd, a.stk, a.ns, o);
    if (fl & RC_TR_FIXFN) {
      uint64_t *io = (uint64_t *)(uintptr_t)frame[0];  // first argument: the structure the callback filled in
      if (io) for (unsigned k = 0; k < ((fl >> 8) & 255); k++) io[k] = (uint64_t)rc_tramp_get(io[k]);
    }
    o[0] = extend_ret(o[0], fl & RC_TR_RET_MASK);
  }
  frame[0] = o[0];
  frame[1] = o[1];
  frame[8] = o[2];
  frame[9] = o[3];
}

// A guest C++ object handed to native code that will make virtual calls on it
// (Steam CCallbackBase): point its vtable at a native shadow copy whose slots
// are trampolines (offset-to-top and RTTI copied, so guest dynamic_cast still
// works). Guest virtual calls through the shadow reach the same functions via
// rc_icall's native branch. One shadow per guest vtable.
void rc_shadow_vtable(uint64_t obj, int nslots) {
  static struct { uint64_t vt; uint64_t *shadow; } map[256];
  static int nmap;
  if (!obj) return;
  uint64_t *o = (uint64_t *)obj, vt = o[0];
  if (!rc_in_image(vt)) return;  // already native (or shadowed)
  const uint64_t *g = (const uint64_t *)vt;
  pthread_mutex_lock(&mu);
  uint64_t *sh = 0;
  for (int i = 0; i < nmap; i++)
    if (map[i].vt == vt) sh = map[i].shadow;
  pthread_mutex_unlock(&mu);
  if (!sh) {
    sh = calloc((size_t)nslots + 2, 8);
    sh[0] = g[-2];
    sh[1] = g[-1];
    for (int i = 0; i < nslots; i++) sh[2 + i] = (uint64_t)rc_tramp_get(g[i]);
    pthread_mutex_lock(&mu);
    if (nmap == 256) rc_fail("too many shadow vtables", vt - rc_slide);
    map[nmap].vt = vt;
    map[nmap++].shadow = sh;
    pthread_mutex_unlock(&mu);
  }
  o[0] = (uint64_t)(sh + 2);
}
