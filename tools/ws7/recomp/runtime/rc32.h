// Runtime of 32-bit guests (xlat/x86_32.py, build/gen_image_pe.py).
//
// Memory model: one 4 GB window reserved at start (rc32_mem.c). A guest address g is the host
// address rc_mem + (uint32_t)g, so 32-bit wrap-around is the cast. The image is mapped at its
// preferred base inside the window; every guest stack, heap, TEB and object a guest may hold a
// pointer to lives there too. Native pointers cross with RC32_H / RC32_G.
//
// Registers: rc_ctx as for 64-bit guests (r[0..7] = eax..edi, zero-extended; x[0..7]), plus the
// x87/MMX state and the FS base (rc.h, under RC_GUEST32).
#ifndef RC32_H_INCLUDED
#define RC32_H_INCLUDED
#ifndef RC_GUEST32
#error "compile 32-bit guest code with -DRC_GUEST32"
#endif
#include "rc.h"

extern uint8_t *rc_mem;  // host address of guest address 0
#define RC32_H(g) ((void *)(rc_mem + (uint32_t)(g)))
#define RC32_G(p) ((uint32_t)((uint8_t *)(p) - rc_mem))

// --- the window (rc32_mem.c)
// Reserve the window (idempotent); 0 on success.
int rc32_mem_init(void);
// Commit [g, g + size) read/write (page-rounded); 0 on success.
int rc32_mem_commit(uint32_t g, uint32_t size);
// A guest heap block inside the window (bump allocator over a committed arena, 16-aligned;
// never freed). 0 when the arena is full.
uint32_t rc32_alloc(uint32_t size);

// --- the image (rc32_image.c)
// The translated functions, sorted by guest address (gen_image_pe.py: table.c).
typedef struct { uint32_t addr; rc_fn fn; } rc32_entry;
extern const rc32_entry rc32_table[];
extern const int rc32_table_len;
// Imports: IAT slot, "dll!name" and the thunk (gen_image_pe.py: imports.c). A thunk is called
// with the arguments at guest esp (no return address) and removes them itself when the
// convention is stdcall.
typedef struct { uint32_t slot; const char *name; rc_fn fn; } rc32_import;
extern const rc32_import rc32_imports[];
extern const int rc32_imports_len;
// Guest address the IAT slot of import k holds once loaded: inside a reserved block of the
// window, one word per import, so a stored and later called import pointer still dispatches.
extern uint32_t rc32_imp_area;
// Map the PE file at its preferred base (sections, zero-filled tails; no relocation needed at
// the preferred base) and fill the IAT; 0 on success.
int rc32_image_load(const char *path);
// The translated function at guest address g, or 0.
rc_fn rc32_lookup(uint32_t g);
// An indirect call or jump to g (the return address, for a call, is already pushed): a
// translated function, or an import's thunk (return address popped first), else rc_fail.
void rc32_icall(rc_ctx *c, uint32_t g);
// rdtsc: a monotonic host tick count.
uint64_t rc32_rdtsc(void);

// --- threads (rc32_teb.c)
// The calling thread's context: guest stack and TEB inside the window, c->fs set. Created on
// first use.
rc_ctx *rc32_thread_ctx(void);
// TEB fields the runtime keeps (offsets of the 32-bit NT_TIB / TEB).
#define RC32_TEB_SEH 0x00      // ExceptionList: the innermost EXCEPTION_REGISTRATION_RECORD
#define RC32_TEB_STACKBASE 0x04
#define RC32_TEB_STACKLIMIT 0x08
#define RC32_TEB_SELF 0x18
#define RC32_TEB_PID 0x20
#define RC32_TEB_TID 0x24
#define RC32_TEB_TLSPTR 0x2c   // ThreadLocalStoragePointer (static TLS, __declspec(thread))
#define RC32_TEB_LASTERR 0x34
#define RC32_TEB_TLSSLOTS 0xe10  // TlsGetValue slots 0..63
#define RC32_TEB_SIZE 0x1000

// Native -> guest: run the guest function at g on c with the given 32-bit stack arguments
// pushed right to left and a return address; ecx/edx as given (thiscall / fastcall). Returns
// eax (edx in *hi when hi is not 0). The callee's own `ret N` decides who removes the
// arguments; esp is restored either way.
uint32_t rc32_call(rc_ctx *c, uint32_t g, const uint32_t *args, int nargs, uint32_t ecx,
                   uint32_t edx, uint32_t *hi);

// --- structured exception handling (rc32_seh.c)
// Dispatch an exception the way ntdll does for a frame-based (x86) handler chain: build the
// EXCEPTION_RECORD and CONTEXT on the guest stack, walk FS:[0] calling each handler
// (cdecl: record, frame, context, dispatcher context) until one returns
// ExceptionContinueExecution (0: resume with the CONTEXT) or a handler unwinds (rc32_unwind)
// and transfers control to its target with rc32_resume. Unhandled: rc_fail.
void rc32_raise(rc_ctx *c, uint32_t code, uint32_t flags, uint32_t nargs, const uint32_t *args);
// RtlUnwind(TargetFrame, TargetIp, ExceptionRecord, ReturnValue): call every handler from FS:[0]
// up to TargetFrame with EXCEPTION_UNWINDING and unlink it.
void rc32_unwind(rc_ctx *c, uint32_t target_frame, uint32_t record, uint32_t retval);
// Continue at guest address ip with the registers in c (esp/ebp as set): longjmp to the newest
// live generated frame that registered ip as a resume point (rc32_resume_enter), discarding the
// host frames above it. The translator registers resume points in functions that install an SEH
// frame: the handler targets of their scope tables (rc32_seh.c).
_Noreturn void rc32_resume(rc_ctx *c, uint32_t ip);
// Called by a function that installs an SEH frame, before its first install: registers the
// host frame (jmp_buf) for the guest frame `ebp`, with the resume points of the function.
// Returns the record whose jmp_buf the function setjmps on; rc32_resume_leave drops it.
typedef struct rc32_resume_rec rc32_resume_rec;
rc32_resume_rec *rc32_resume_enter(rc_ctx *c, uint32_t func, const uint32_t *ips, int nips);
void rc32_resume_leave(rc_ctx *c, rc32_resume_rec *r);
void *rc32_resume_jb(rc32_resume_rec *r);
uint32_t rc32_resume_ip(rc32_resume_rec *r);

#endif
