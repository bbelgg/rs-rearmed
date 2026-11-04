// COM objects of the D3D9 layer for the 32-bit guest (win-x86).
//
// A COM call in the guest is `push args; push this; call [vtbl + 4 * slot]`: an indirect call
// through rc32_icall. Every vtable slot holds one of the seam's thunk cells (win32_guestobj.h,
// W32_STDCALL: every COM method is stdcall), so a method is a w32_hostfn that finds `this` in a[0]
// and the other stack arguments in a[1..].
//
// An object is a block in the window: [0] vtable, [4] 'D3D9', [8] index into the host object
// table. The guest only ever reads the vtable word; everything else lives on the host.
#ifndef D3D9_THUNK_H
#define D3D9_THUNK_H

#include <stdint.h>
#include "rc32.h"
#include "win32_guestobj.h"

// One vtable slot: {name, stack words including `this`, 0, method}. A slot with no method is
// logged once and returns D3DERR_INVALIDCALL, the answer D3D9 gives for a bad call.
typedef w32_slot d3d9_slot;

// Guest address of a vtable with these slots (built once per table: cached by its address).
static inline uint32_t d3d9_vtable(const d3d9_slot *slots, int n) {
    return w32_vtable(slots, n, W32_STDCALL, 0x8876086Cu);
}

// --- objects
#define D3D9_OBJ_MAGIC 0x39443344u  // "D3D9"
typedef struct d3d9_obj d3d9_obj;
struct d3d9_obj {
    uint32_t guest;       // the guest object (0 until d3d9_obj_publish)
    int kind;             // D3D9K_*
    int32_t ref;          // public COM count
    int32_t iref;         // internal: bindings and state blocks; destroyed when both are 0
    uint32_t index;       // in the object table
    void (*destroy)(d3d9_obj *o);
    d3d9_obj *fwd;        // counts go to this object instead (a texture's level surfaces)
};
// Give o a guest object with this vtable; o->ref = 1.
uint32_t d3d9_obj_publish(d3d9_obj *o, uint32_t vtable);
// The host object behind a guest pointer, or 0 (wrong magic, stale, or not of `kind` when
// kind >= 0).
d3d9_obj *d3d9_obj_get(uint32_t guest, int kind);
uint32_t d3d9_obj_addref(d3d9_obj *o);
uint32_t d3d9_obj_release(d3d9_obj *o);
void d3d9_obj_ihold(d3d9_obj *o);   // o may be 0
void d3d9_obj_idrop(d3d9_obj *o);   // o may be 0
// Take o out of the table and free its guest block, without running destroy (for objects
// whose lifetime another object owns).
void d3d9_obj_kill(d3d9_obj *o);

// Guest memory accessors.
static inline uint32_t d3d9_rd32(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static inline void d3d9_wr32(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
static inline float d3d9_f32(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }

// Log once per key string (thread-safe enough for diagnostics).
void d3d9_log_once(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
