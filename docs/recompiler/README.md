# The recompiler

`tools/ws7/recomp` is the static recompiler both ports are built with. It translates a game binary's
machine code to C once, at build time, and the C is compiled for arm64. Nothing is translated while the
game runs. It has two front ends, one per game binary, and one back end:

| | Mac port | Windows port (Learn & Play) |
|---|---|---|
| Input | x86-64 Mach-O, SysV ABI | 32-bit x86 PE, MSVC ABI (`cdecl`, `stdcall`, `thiscall`, `fastcall`) |
| Front end | `xlat/core.py` | `xlat/x86_32.py` (`Translator32`, the same emitters in 32-bit mode) |
| Generator | `build/gen_image.py` | `build/gen_image_pe.py` |
| Guest memory | the image at its slide in the host address space | a 4 GB window; guest address = `rc_mem + (uint32_t)addr` |
| Runtime | `runtime/rc_*` | `runtime/rc_*` plus `runtime/rc32_*` (`-DRC_GUEST32`) |
| Tables | `tools/targets/mac-x64` | `tools/targets/win-x86` (965 of 1,156 symbols read), `win-x86/imports` (import sites), `tools/targets/win-x86/target.toml` |

How each port uses it: [Mac design](../mac/DESIGN.md), [Windows design](../windows/DESIGN.md).

## What the C looks like

Every function of the image becomes `F_<addr>(rc_ctx *c)`. Guest registers live in `rc_ctx`; the
arithmetic flags are C locals of the function (exact for compiler-generated code, which never reads flags
across a call; the few hand-written 32-bit helpers that answer in a flag, or leave the caller's alone, pass
them through `c->ret_flags`). Calls between
translated functions are C calls; indirect calls and jumps go through a
lookup of the target address (`rc_lookup`, `rc32_icall`). Jump tables are resolved at translation time
(`xlat/jumptab.py`). Calls to imports go to thunks generated from prototypes (`thunks/`).

The generator chunks the functions into translation units by instruction count, writes `decls.h`,
`table.c` (address to function), `manifest.json` (per function: status, instruction count, inferred
convention) and a `build.ninja` that makes `libgame_rc.a`. A function the front end cannot translate gets
a stub that fails with its address when called. Translation is deterministic: the same image and options
give the same C whatever `--jobs` or `--batch` is (`gen_image.py` decides every function's no-return
status once, in address order, before the workers start).

## Per-game tables

Everything tied to one binary (function addresses, calling conventions, struct offsets) lives in
`tools/targets/<target>/`; the rest of the tree uses names. See [tools/targets/README.md](../../tools/targets/README.md).

- `RC_TARGET` (default `mac-x64`) picks the target. `targets.py gen OUTDIR/target` writes
  `rc_target_names.h` (`RC_ADDR_`, `RC_FN_`, `RC_CC_`, `RC_OFF_` defines), `rc_target.c` (hook-point names
  for the runtime) and the skip lists for the generator.
- Native code reads game state through `runtime/rc_abi.h` (`RC_ADDR`, `RC_FN`, `RC_GA`, `RC_OFF`,
  `RC_SLOT`, `RC_HOST`/`RC_GUEST`) and `overrides/rc_ov.h` (`OV_GUEST(name, ...)`, `OV_ARGS_OF`,
  `OV_ALIAS`). No literal addresses or offsets in code. The differential tests (`shims/game/harness`,
  `shims/game/test`) name the originals they run the same way, through mac-x64's tables.
- `tools/targets/check.py` (ctest `targets.check`) fails when a name in `names/` has no entry in a target
  that does not mark it `unsupported`, or when native code uses a name no list has.

`mac-x64` is a full target. `win-x86` has an entry for every name, `check.py` covers both: 965 of the 1,156
symbols and 77 of the 98 offsets are read from the Windows code, and an `unsupported` line says why for the
rest (inlined by LTCG, Mac-only, not needed, or not proved). Its `target.toml` also holds the install pins.
The Windows build uses it for the hook points (`gen_image_pe.py --target`) and for what a mod asks for by name;
its built-in fixes still name addresses in `win32_overrides.c`.

## Overrides and hook points

- **Overrides.** An override module (`tools/ws7/recomp/overrides`) defines `F_<addr>` for a game function
  in native code; the generator is told with `--skip FILE` and does not generate it (`ADDR drop`: not
  generated and not in the table). The Mac port replaces whole libraries this way (zlib, libpng, libjpeg,
  Lua, the crypto, the efd framework, the audio engine's CoreAudio side, the online layer, the built-in
  fixes).
- **Hook points.** `runtime/rc_hook.h`: `rc_hook_add_name(name, pre, post, user)` runs code before and
  after a named game function (`tools/targets/names/hooks.list`); `rc_hook_add(addr, ...)` takes a Mac
  address; `rc_target()` names the target. Both apps export this runtime to mods. A name the game doesn't
  have (`unsupported` in its `symbols.map`) gives -1 and a log line, at load, in either edition.
- **What a hook sees.** The same on every target: the Mac's SysV call. On the 32-bit guest a hook's `rc_ctx *`
  is a view of the call made from the target's tables (`runtime/rc32_hook.c`), not the guest's context: the
  arguments in `rdi rsi rdx rcx r8 r9` and `xmm0-7` whatever the convention, a guest pointer as a host
  pointer (null stays null), the stack pointer on a host copy of the return address. What a hook changes goes
  back: arguments before the original runs, the result after it (to `eax`, `edx:eax` or `st0`, by the prototype),
  and the guest's `ret N` for a hook that does the call itself. So `rc_args` and `ov_ret_*`, which a mod has
  inline, mean the same in both. `targets.py` makes the table from each hook point's prototype in
  `symbols.map`. A hook point of a name is the Mac's call on every target, and `targets.py` compares the two
  prototypes. What differs is adapted there: register arguments of a function of its own (`custom`) are named
  (`const char *path@ecx`), an argument the compiler folded away has its one value (`int event=0x98`), one the
  Mac's call hasn't is hidden, a result of another form converted
  ([the forms](../../tools/targets/README.md#hook-points-on-a-target-with-another-abi)). A point that still isn't the
  Mac's call, a function whose prototype doesn't say where its arguments are, or a stdcall, thiscall or
  fastcall one whose stack arguments don't add up to its own `ret N` (`rc_hook_code_pops`, from the translator;
  the translation warns too), is refused with a log line. A cdecl or `custom` function's `ret` says nothing of its
  arguments, so there the prototype in the table is all there is. `RC_HOOK_TRACE=name,name` logs a point's raw calls,
  refused ones too, to check a table in a run. On the Mac the view is not needed: the hook gets the context, by
  the same code as ever.
- **Mods.** `runtime/rc_mod.h` is what a mod includes, one source and one arm64 dylib for both editions:
  `RC_OFF`, `RC_SLOT`, `RC_ADDR` and `RC_GA` look the name up when the mod runs (`rc_off`, `rc_addr`, from the
  tables in `rc_target.c`), a guest pointer is read with `rc_mod_ptr` and `RC_HOST`, and `OV_GUEST`
  calls by the game's convention (`rc_call_name`; `rc_call_name_f` with a float among the arguments). What the game is
  given has to be its own memory: `rc_guest_alloc`, malloc on the Mac, the guest's arena on a 32-bit guest; a pointer
  it can't see reaches it as null, with a log line. `runtime/rc_mods.c` is the `mods/` folder for both apps.
  Learn & Play loads only a library that has `rc_mod_abi` (`rc_mod.h` defines it): one built without it has the
  Mac's offsets compiled in and reads a pointer as 8 bytes, so none of its code runs. The Mac loads both.
- **Native code on the guest's own context** (the overrides): `OV_ARGS_OF(name, a, c)` reads by the name's
  convention, `OV_ARGS_CC(a, c, cc)` is the same for a helper that serves several functions, and neither compiles
  for a `custom` function: those are read by hand. `ov32_ret` and `ov32_ret_f` end an override.
- **Windows.** The 32-bit runtime overrides an export of a translated DLL by name
  (`rc32_override_add(dll, name, fn)`, used for msvcr100's math) and replaces virtual methods by writing
  host thunk cells into game vtables (the online layer).

## The 64-bit front end (Mac)

`gen_image.py IMAGE OUT [--skip FILE]... [--o1 FILE] [--tso-ranges FILE] [--target T]` translates every
function start in `__text`. Memory ordering: x86 is TSO and arm64 is not, so translation units get
`-DRC_MO_TSO` and their memory accesses are fenced, except RIP-relative loads from sections nothing writes
after load (`RC_LDC`) and the functions in `tools/targets/names/relaxed.list` (audio kernels, fenced only at
entry and exit; `RC_RELAXED=0` fences them too). `--o1 FILE` compiles listed functions alone at `-O1`.
`build/README.md` lists the build's switches ([tools/ws7/recomp/build/README.md](../../tools/ws7/recomp/build/README.md)).

## The 32-bit front end (Windows)

`gen_image_pe.py IMAGE OUT [--jobs N] [--wrappers TSV] [--module NAME] [--skip FILE] [--target ID] [--opt -O2]`.

A `--skip` line `ADDR` is an override, as on the Mac. A line `ADDR wrap` keeps the function translated, as
`W_<addr>`, and the override module's `F_<addr>` (what every call and `rc32_table` reach) runs native code
and calls `W_<addr>` for the original (main image only).

`--target ID` makes the target's hook points: each is translated as `W_<addr>`, and `table.c` defines its
`F_<addr>`, which is one load and a branch more than the function until a hook is registered (`rc_hook_on`). A
hook point that is also an override is `N_<addr>` in the override module, and the hooks run around that.

### Decode and memory

- Capstone `CS_MODE_32`. A memory operand is `RC_M(guest) = rc_mem + (uint32_t)guest`, so address wrap is
  the cast; `lea` and address arithmetic wrap at 32 bits. `FS:` operands address the per-thread TEB
  (`c->fs`).
- 4-byte stack slots; `call` pushes a 32-bit return address; `ret N` adds `4 + N` to `esp`. The split
  between caller and callee of `stdcall`/`thiscall`/`fastcall` needs nothing more inside translated code.
- `call/jmp [IAT slot]` goes to the import's thunk `I_<slot>`; any other indirect transfer goes to
  `rc32_icall`.
- The direction flag, `pushad`/`popad`, `pushfd`/`popfd` (bits outside the modelled ones are carried, so
  the CPUID-availability toggle works; `popfd` with TF raises a single-step exception), `enter`/`leave`,
  the `loop` family, `rdtsc`, `xlat`, segment-register reads as flat selectors.
- **Packer import sites** (`--wrappers TSV`: call site, stub, kind, resume address, pushed register,
  import). VMProtect turned each `call/jmp [slot]` into a call to a stub. A listed site decodes as the
  original import call and decoding continues at the resume address. An import the IAT lacks gets a slot
  of its own. An unnamed one calls `rc32_wrapper_unresolved`, which logs the site and fails.
- `runtime/rc32_mem.c` reserves the 4 GB window at start (`PROT_NONE`, committed in pieces; the first
  64 KB stay unmapped so a null pointer faults) and hands out guest stacks, TEBs and heap from an arena
  that steps over the images. A thread's stack is what `CreateThread` asked for, at least the main
  image's `SizeOfStackReserve` and 1 MB, with a guard below it.

### Memory ordering

As on the Mac: every translation unit of every image, the DLLs too, gets `-DRC_MO_TSO`
(`gen_image_pe.py`), so memory accesses are fenced. Without it clang hoists a load out of a spin loop (`b .`
in the object code: a thread that reaches it with the flag set hangs at 100% of a core) and arm64 reorders
what the game publishes to its other threads. Not fenced: `esp`-based and frame-pointer accesses (`RC_LDS`,
`RC_STS`), and loads from a fixed address in a section nothing writes, a constant or a table of them
(`read_only`, `RC_LDC`). A `rep movs` or `rep stos` taking the `memmove`/`memset` path is fenced before and
after, not store by store. `relaxed.list` does not apply yet: none of its functions is matched in `win-x86`.

### x87 and MMX

`xlat/emit_x87.py` decodes x87 from the escape opcode and ModRM. The register stack is doubles in
`rc_ctx.fpu` (`runtime/rc32_x87.h`): loads and stores, arithmetic, compares (`fcom*`, `fucom*`, `fcomi*`,
`ftst`, `fxam`), control and status words, `fprem(1)`, `frndint`, `fscale`, `fxtract`, the
transcendentals, `fldenv`/`fnstenv`/`fnsave`/`frstor`, and the `fnstsw ax; test ah` and `_ftol2` idioms.
Values are computed as `double`, which is what MSVC's default precision control gives. MMX maps onto the
SSE helpers.

### Conventions

The convention of each function is inferred (the first instructions' use of `ecx`/`edx`, the `ret` form)
and recorded in `manifest.json` (`{"pops": N, "conv": "cdecl|stdcall|thiscall|fastcall"}`). It is a
default, not ground truth. `rc32_call` calls the guest from native code with stack arguments and
`ecx`/`edx` for `thiscall`/`fastcall`.

### SEH and C++ exceptions

`runtime/rc32_seh.c` is a frame-based dispatcher: the `FS:[0]` chain, `RaiseException`, `RtlUnwind`. A
function with resume points (`__except` blocks, catch continuations, the return of a `setjmp` call) `setjmp`s
at entry; a jump to a resume point `longjmp`s there, dropping the host frames in between. `__except` blocks
belong to the function that sets up the frame (scope tables read from the `__SEH_prolog4` and the push
set-ups). So do catch continuations (`cxx_scopes`: the frame's `__ehhandler` thunk, its `FuncInfo`, the catch
funclets and the `mov eax, offset CONT; ret` that ends each), or to the funclet of the catch the `try` is
inside. `_CxxThrowException`, `__CxxFrameHandler3`, `_setjmp3` and `longjmp` are the CRT's own code,
translated: a `setjmp` call keeps its real return address, which `longjmp` then jumps to. `RC32_LOG_RAISE=N`
logs the first N exceptions and the first N resumes.

Only exceptions the guest raises itself are delivered: a host fault (access violation), `#DE` or `int3` ends
the process with the fault logged, and an `__except` around one does not run. At a fault the guest's registers
are in C locals of the generated code, not in `rc_ctx`, so there is no `CONTEXT` to hand a filter or a stack
to run it on; nothing the game was seen to do needs it.

### PE images, several per process

`xlat/pe.py` reads sections, relocations, imports, exports, TLS and the SafeSEH table. Function discovery
seeds from the entry, exports, TLS callbacks and SafeSEH handlers, then follows direct calls, relocated
code pointers (relocated words in code sections, except the entries of indexed tables) and the gaps between
functions, rejecting pieces that decode to instructions MSVC never emits.

Several images share one process (`runtime/rc32_image.c`):

- Each image is generated as its own tree; `--module NAME` prefixes its globals (`NAME_rc32_mod`,
  `NAME_I_<slot>`). `F_<addr>` names are unique by address.
- `rc32_image_add(&mod, path)` per DLL, then `rc32_image_load(game)` or `rc32_images_bind()`. Every image
  runs at its preferred base; the loader refuses overlaps. A DLL that must live elsewhere is translated
  from a rebased copy (`win-x86/d3dx/rebase_pe.py`).
- An import of another loaded image's export (by name or ordinal, forwarders followed) is bound to the
  export's real address, so data exports work. Other imports go to the host: `rc32_import_call` for the
  main image, `rc32_host_import` for a module.
- `rc32_images_init` runs `DllMain(DLL_PROCESS_ATTACH)` in dependency order and registers the detach
  calls with `atexit`. `rc32_image_defer` keeps a DLL for `LoadLibrary`. `rc32_image_base`,
  `rc32_image_export` and `rc32_image_alias` answer `GetModuleHandle`, `GetProcAddress` and a second name
  (`msvcrt.dll` for msvcr100).
- `rc32_icall` offers an address no image knows to registered host functions
  (`rc32_hostfn_hook_add`) before failing; the Win32 seam's COM objects are host functions in guest
  vtables this way.

### Known gaps

- AVX/AVX2 and SSE4.2 string paths are not translated; the CPUID the runtime reports selects the SSE2
  paths, so they are dead. The same for 3DNow! in d3dx9's math library.
- A few functions whose computed jump tables overlap their own code (the CPUID parser in
  `systemdetection.dll`) leave an inner jump unresolved and fail if reached; nothing in the game reaches
  them.
- `DLL_THREAD_ATTACH`/`DETACH` are not sent.

## Tests

The front end's differential test (`test/x86/difftest.py`) runs each translated function and the original
bytes under Unicorn from the same seeded state and compares the end state: status, the GPRs, xmm0-7, the
x87 stack, MMX, and the stack, scratch, TEB and writable sections. `test/x86/handcases.py` builds a small
PE of hand-written snippets (x87 idioms, flag corners, the four conventions, string ops, MMX, SSE, jump
tables, discovery cases), so these paths are tested without any DLL. How to run them:
[Windows testing](../windows/TESTING.md#the-recompiler).

Exceptions need the CRT, so the difftest does not reach them. `test/x86/ehcases.py` builds a small PE of
hand-written exception code, the frames and tables MSVC writes (a catch only its continuation leaves, a throw
from below a frame with a destructor, a `try` inside a catch, `__except` around an inner frame, `setjmp` and
`longjmp` through the IAT and through a thunk, with a frame to unwind, an inner `setjmp`). `handcases` checks what
the discovery finds in it; `crt_probe -eh` runs it on the real, translated msvcr100.

Known differences the comparator accepts: flags images spilled below the final `esp`, AF, x87 C0/C2/C3
after arithmetic, values beyond `double`'s exponent range, and Unicorn's own results for partial `fprem`
and out-of-domain `fsqrt`/`fyl2x`.

Hook points have a test of their own that needs no game (`tools/ws7/recomp/test/hooks`, ctest `recomp.hooks`): one mod
dylib, built once, runs in the 64-bit and the 32-bit runtime and reads the same call in both, for each
convention (cdecl, stdcall, thiscall, fastcall, register arguments, floats, 64-bit values, stack arguments past
the sixth, folded ones), changes arguments and results, does the call itself and calls game code first.

The 64-bit front end is checked by the Mac build itself: the whole image regenerates byte for byte across
runs and scheduling, and the shims' differential tests compare replaced functions against the original
code ([Mac testing](../mac/TESTING.md)).

## Running it by hand

```sh
cd tools/ws7/recomp
# translate and compile one PE image (arm64)
"$RC_PY" -I build/gen_image_pe.py IMAGE.dll OUTDIR --jobs 8 --cflags=-w
ninja -C OUTDIR
```

The Mac image is built through `app/build-m4.sh` ([Mac install](../mac/INSTALL.md#build-by-hand)), the
Windows images through `win-x86/install/lp_build.py` ([Windows install](../windows/INSTALL.md)). Game
images and Microsoft's DLLs are build inputs only: never commit them or anything generated from them.
