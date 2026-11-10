// Host functions the 32-bit guest can call through a pointer, guest vtables made of them, and
// native -> guest calls from inside a shim (win-x86).
//
// A guest call through a pointer (`call [vtbl + 4 * k]`, a stored callback) reaches rc32_icall
// with an address. A host function gets one here: a thunk cell, one word in a reserved block of
// the window (int3 bytes, never executed), as the import area gives one to each import.
// rc32_icall hands an address it does not know to w32_hostfn_call (registered with
// rc32_hostfn_hook_add when the first cell is made), which finds the cell, pops the return
// address into c->ret_to, reads the stack words, runs the host function and removes the
// arguments when the convention says the callee does.
//
// Conventions:
//   W32_STDCALL   every argument on the stack, callee removes them (COM, WINAPI);
//   W32_THISCALL  `this` in ecx, the rest on the stack, callee removes them (MSVC C++ methods);
//   W32_CDECL     every argument on the stack, the caller removes them.
// A host function sees `self` (ecx for thiscall, else 0) and the stack words a[0..words-1]. For a
// method returning a class by value (MSVC member functions return every class through memory),
// a[0] is the hidden result pointer and the function returns it.
//
// The D3D9 layer's COM objects (shims/d3d9), the audio faces (shims/audio) and the Steam face
// (shims/steam) all use these cells, vtables and guest memory.
#ifndef WIN32_GUESTOBJ_H
#define WIN32_GUESTOBJ_H

#include <stdint.h>
#include <string.h>

#include "rc32.h"

enum { W32_STDCALL, W32_THISCALL, W32_CDECL };

// Returns edx:eax.
typedef uint64_t (*w32_hostfn)(rc_ctx *c, uint32_t self, const uint32_t *a);

// Guest address of a new thunk cell (never freed); 0 when the cells run out. fn 0: logs the name
// once and returns dflt (the words are still removed as the convention says).
uint32_t w32_hostfn_new(const char *name, int conv, int words, w32_hostfn fn, uint64_t dflt);

// rc32_icall's hook (rc32_hostfn_hook): 1 when g is a thunk cell (the call has then run), else 0.
int w32_hostfn_call(rc_ctx *c, uint32_t g);
// Set by the frame log (win32_frame.c, LP_FRAME_LOG) when it is on: every host function call is
// then timed and reported by name.
extern void (*w32_call_timed)(const char *name, uint64_t ns);

// One vtable slot. sret: the method returns a class through a hidden pointer (a[0]); a slot with
// no fn then zero-fills the 8-byte result and returns the pointer.
typedef struct {
  const char *name;
  uint8_t words;  // stack words, not counting a thiscall `this`
  uint8_t sret;
  w32_hostfn fn;
} w32_slot;

// Guest address of a vtable of these slots (cells made once per table: cached by its address).
uint32_t w32_vtable(const w32_slot *slots, int n, int conv, uint64_t dflt);
// A guest object whose first word is vtable (size bytes, zero-filled, never freed).
uint32_t w32_object(uint32_t vtable, uint32_t size);

// Native -> guest from inside a shim or host function: rc32_call with the shim's own state kept.
// rc32_call leaves the registers as the callee left them, and a host function called here sets
// c->ret_to; the guest code that called the shim compares c->ret_to with its return address when it
// called through a pointer, so both are put back. Returns eax (edx in *hi when hi is not 0).
uint32_t w32_guest_call(rc_ctx *c, uint32_t fn, int conv, uint32_t self, const uint32_t *args,
                        int nargs, uint32_t *hi);

// Guest memory that can be freed (rc32_alloc never frees): power-of-two classes over rc32_alloc,
// zero-filled. 0 when out of memory.
uint32_t w32_galloc(uint32_t size);
// The same block with only its first `clear` bytes sure to be zero: for a caller that clears the
// rest itself as it comes into use (the heap, whose blocks grow into their class).
uint32_t w32_galloc_part(uint32_t size, uint32_t clear);
void w32_gfree(uint32_t g, uint32_t size);
// A guest copy of a NUL-terminated host string, interned: the same text gives the same address,
// which stays valid for the life of the process (for `const char *` results). 0 for 0.
uint32_t w32_gstr(const char *s);

static inline uint32_t w32_rd32(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static inline void w32_wr32(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
static inline void w32_wr64(uint32_t g, uint64_t v) { memcpy(RC32_H(g), &v, 8); }
static inline uint64_t w32_u64(const uint32_t *a) { return a[0] | (uint64_t)a[1] << 32; }
// Host view of a guest pointer, 0 for a null one.
static inline void *w32_h(uint32_t g) { return g ? RC32_H(g) : 0; }

#endif
