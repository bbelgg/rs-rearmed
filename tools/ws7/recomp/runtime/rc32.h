// Runtime of 32-bit guests (xlat/x86_32.py, build/gen_image_pe.py).
//
// Memory model: one 4 GB window reserved at start (rc32_mem.c). A guest address g is the host
// address rc_mem + (uint32_t)g, so 32-bit wrap-around is the cast. Each image is mapped at its
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
void rc32_mem_guard(uint32_t g, uint32_t size);  // no access: a page-aligned range, a stack's guard
// A guest heap block inside the window (bump allocator over a committed arena, 16-aligned;
// never freed; it steps over the ranges of mapped images). 0 when the arena is full.
uint32_t rc32_alloc(uint32_t size);
extern uint64_t rc_img_lo, rc_img_hi;  // the game's image, host addresses: what a mod reads as on the Mac (rc_rt.h)
uint32_t rc32_arena_used(void);  // bytes of the arena handed out so far (it never shrinks)
// Keep [g, g + size) for an image: the arena will not hand it out. -1 when it overlaps another
// image's range or arena memory already handed out.
int rc32_mem_reserve(uint32_t g, uint32_t size);

// --- images (rc32_image.c, rc32_main.c)
// A process holds several translated PE images: the game and the DLLs translated with it
// (msvcr100, msvcp100), each mapped at its own base inside the window. Each generated tree
// (gen_image_pe.py) describes itself with an rc32_module; a DLL's tree is generated with
// --module NAME, so its globals are NAME_rc32_table, NAME_rc32_mod, ... and several trees link
// into one binary. The main image keeps the plain names.
//
// The translated functions, sorted by guest address (gen_image_pe.py: table.c).
typedef struct { uint32_t addr; rc_fn fn; } rc32_entry;
// Imports: IAT slot, "dll!name" (the DLL name in lower case) and the thunk (gen_image_pe.py:
// imports.c). A thunk is called with the arguments at guest esp (no return address) and removes
// them itself when the convention is stdcall.
typedef struct { uint32_t slot; const char *name; rc_fn fn; } rc32_import;
typedef struct rc32_image rc32_image;
typedef struct rc32_module {
  const char *name;            // the module (--module NAME), "" for the main image
  const rc32_entry *table;
  const int *table_len;
  const rc32_import *imports;
  const int *imports_len;
  // the host side of an import no loaded image exports: the main image's is rc32_import_call
  // (the shims' dispatcher, by index into rc32_imports); 0: rc32_host_import
  void (*host)(rc_ctx *c, int k);
  rc32_image *image;           // the runtime's, once loaded
} rc32_module;
// The main image's tree (no --module): plain names.
extern const rc32_entry rc32_table[];
extern const int rc32_table_len;
extern const rc32_import rc32_imports[];
extern const int rc32_imports_len;
extern rc32_module rc32_mod;
// The host dispatcher of the main image's imports: the shims (or a test runner) define it; k
// indexes rc32_imports.
void rc32_import_call(rc_ctx *c, int k);
// The host dispatcher of a --module image's imports that no loaded image exports (a DLL's
// kernel32): m->imports[k].name says which. Set it before the DLLs run; while it is 0 such a
// call fails with the import's name.
extern void (*rc32_host_import)(rc_ctx *c, const rc32_module *m, int k);
// What every generated thunk calls: the export of another loaded image when the import was
// bound to one (rc32_images_bind), called as the guest would have called it; else the host
// (m->host, or rc32_host_import).
void rc32_import_thunk(rc_ctx *c, rc32_module *m, int k);

// Map an image's PE file at its preferred base (sections, zero-filled tails; translated code
// holds the preferred addresses, so an image never moves) and register it; its imports are
// bound by rc32_images_bind, once every image is mapped. Fails (0) when its range overlaps
// another image or guest memory already handed out.
rc32_image *rc32_image_add(rc32_module *m, const char *path);
// Fill every image's IAT. An import of a loaded image's export ("msvcr100.dll!_iob", by name or
// #ordinal, forwarders followed) gets the export's guest address in its slot: game code reads a
// data export through its slot, and a function pointer read from it resolves through
// rc32_lookup; a direct call through the slot runs the translated export (rc32_import_thunk).
// Any other import's slot gets a cell of the image's import area (one word per import, never
// executed), so a stored-then-called import pointer still dispatches to the host. 0 on success.
int rc32_images_bind(void);
// A native override of an export ("msvcr100.dll", "_CIpow"): every call that reaches the export
// through the registry runs fn instead of the translated function: imports of it in any image
// (bound by rc32_images_bind) and calls through a pointer to it (rc32_lookup, rc32_icall,
// rc32_call). Calls inside the exporting image's own translated code (direct calls between its
// functions) are not affected. fn is called as the translated export would be: the return
// address at guest esp; it pops it into c->ret_to, as a guest `ret` does (removing its
// arguments too when the export is stdcall), and leaves ecx/edx/x87 results in c as the export
// does. Add overrides before rc32_images_bind; the same dll!name again replaces the earlier fn.
// An override whose DLL is not loaded is ignored. 0 on success.
int rc32_override_add(const char *dll, const char *name, rc_fn fn);
// The main image (rc32_mod) with the plain names, then rc32_images_bind (rc32_main.c); 0 on
// success. A process with DLLs adds them first (rc32_image_add), so the binding sees them.
int rc32_image_load(const char *path);
// DLL start-up: DllMain(base, DLL_PROCESS_ATTACH, 0) of every image but the main one, each
// after the DLLs it imports from (msvcr100 before msvcp100), on c. The main image's entry point
// is the caller's to run after this. Registers rc32_images_fini with atexit. 0 on success; a
// DllMain that returns FALSE stops it (-1).
int rc32_images_init(rc_ctx *c);
// DllMain(base, DLL_PROCESS_DETACH, 0) of the started DLLs, in the reverse order, on the calling
// thread's context; once (later calls do nothing).
void rc32_images_fini(void);
// Base of the loaded image whose DLL name is `dll` ("msvcr100.dll", case-insensitive; the
// extension and a path's directories may be there or not; an alias counts), the main image's
// for 0; 0 when none (GetModuleHandle, and LoadLibrary before it starts the image).
uint32_t rc32_image_base(const char *dll);
// Another name an image answers to, for imports and lookups ("msvcrt.dll" for msvcr100, which
// d3dx9_42 imports). 0 on success.
int rc32_image_alias(const char *dll, const char *alias);
// Leave an image out of rc32_images_init: it starts when LoadLibrary asks for it
// (rc32_image_start), or when a DLL that imports from it starts (D3DCompiler_42, which the
// game loads at run time).
void rc32_image_defer(rc32_image *im);
// DllMain(PROCESS_ATTACH) of the image at base, after the DLLs it imports from, unless it has
// started already (LoadLibrary). 0 on success.
int rc32_image_start(rc_ctx *c, uint32_t base);
// Guest address of an export of the image at `base`: by name, or by ordinal when name < 0x10000
// (GetProcAddress's convention, as a host value); forwarders followed. 0 when none.
uint32_t rc32_image_export(uint32_t base, const char *name);
// Guest address the main image's IAT cells start at (one word per import).
extern uint32_t rc32_imp_area;
// The translated function at guest address g (in any image), or 0.
rc_fn rc32_lookup(uint32_t g);
// An indirect call or jump to g (the return address, for a call, is already pushed): a
// translated function, an import's cell (return address popped, then its thunk), a host
// function a hook claims (rc32_hostfn_hook_add), else rc_fail.
void rc32_icall(rc_ctx *c, uint32_t g);
// rc32_lookup's cache (rc32_image.c), and an indirect call through it at the call site: a branch of its own for each
// site for the predictor, where rc32_icall has one for all of them. Whatever isn't a hit goes to rc32_icall.
#define RC32_LOOKUP_BITS 16
#define RC32_LOOKUP_NONE 0x7fffffffu
extern uint64_t rc32_lookup_cache[1u << RC32_LOOKUP_BITS];
#define RC32_ICALL(c, g) do { uint32_t g_ = (g); \
    uint64_t e_ = __atomic_load_n(&rc32_lookup_cache[(g_ * 2654435761u) >> (32 - RC32_LOOKUP_BITS)], __ATOMIC_RELAXED); \
    if (__builtin_expect((uint32_t)(e_ >> 32) == g_ && e_ && (uint32_t)e_ != RC32_LOOKUP_NONE, 1)) \
      ((rc_fn)(uintptr_t)((const char *)(uintptr_t)rc32_lookup + (int32_t)(uint32_t)e_))(c); \
    else rc32_icall(c, g_); } while (0)
// When set, rc32_icall reports every target to it first (diagnostics: LP_PROF_IMPORTS).
extern void (*rc32_icall_trace)(rc_ctx *c, uint32_t g);
// When set, the runtime reports costly events to it (diagnostics: LP_FRAME_LOG): what is
// RC32_NOTE_*, ticks the mach_absolute_time it took (0 when not timed).
enum { RC32_NOTE_LOOKUP_MISS, RC32_NOTE_RAISE };
extern void (*rc32_note)(int what, uint64_t ticks);
// Host functions the guest calls through a pointer (thunk cells of win32_guestobj.c, d3d9):
// rc32_icall offers an address no image knows to each hook in turn; a hook returns 1 when g is
// one of its own (the call has then run: return address popped into c->ret_to, arguments
// removed as the convention says), else 0. Up to 8 hooks; adding one twice is a no-op.
typedef int (*rc32_hostfn_hook)(rc_ctx *c, uint32_t g);
void rc32_hostfn_hook_add(rc32_hostfn_hook h);
// Static TLS (IMAGE_TLS_DIRECTORY32) of the loaded images, in TLS index order: template
// [start, end) + zero fill; rc32_teb.c gives every thread its own copy of each at
// ThreadLocalStoragePointer[index]. The loader writes each image's index into its index slot.
typedef struct { uint32_t start, end, zerofill; } rc32_tls_image;
int rc32_tls_images(const rc32_tls_image **out);
// An import call a packer replaced whose import the wrapper table leaves unnamed (the call site
// and the packer's stub): logs both and fails (rc32_wrappers.c).
_Noreturn void rc32_wrapper_unresolved(rc_ctx *c, uint32_t site, uint32_t stub);
// rdtsc: a monotonic host tick count.
uint64_t rc32_rdtsc(void);

// --- threads (rc32_teb.c)
// The calling thread's context: guest stack and TEB inside the window, c->fs set. Created on
// first use. The stack is what the thread asked for (rc32_thread_stack, before the first use) or
// the default, whichever is more; the default is 1 MB or the main image's SizeOfStackReserve. Below
// it is a guard, so an overflow faults instead of running into the next block of the arena.
rc_ctx *rc32_thread_ctx(void);
void rc32_thread_stack(uint32_t size);
void rc32_stack_default(uint32_t size);
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
