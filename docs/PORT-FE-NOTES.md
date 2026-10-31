# P2: the 32-bit x86 / MSVC / PE front end

Notes on the second front end of the recompiler (`tools/ws7/recomp`), which turns 32-bit x86
PE code into the same `F_<addr>(rc_ctx *)` C the Mac front end produces. This is the P2 work of
[PORT-FROM-WINDOWS.md](PORT-FROM-WINDOWS.md): decode, the 4 GB window, x87, the conventions, SEH
and the PE thunks, with a differential test against Unicorn. It is built and tested against the
plain, unprotected 32-bit DLLs shipped beside the Learn & Play `Rocksmith2014.exe`
(`bink2w32.dll`, `steam_api.dll`, `systemdetection.dll`); the game image itself is not needed yet
and is not here.

## What was built

- **`xlat/x86_32.py`** — the mode switch, not a fork. `Translator32` is `core.Translator` with
  the 32-bit choices; every instruction emitter (`emit_int`, `emit_sse`, `jumptab`) is shared.
  - Capstone `CS_MODE_32`; memory operand → `RC_M(guest) = rc_mem + (uint32_t)guest`, so address
    wrap is the cast; `lea` and address math wrap at 32 bits.
  - 4-byte stack slots; `call` pushes a 32-bit return address; `ret N` adds `4 + N` to esp (the
    caller/callee split of stdcall/thiscall/fastcall needs nothing else inside translated code —
    the caller pushes, the callee pops, exactly as on x86).
  - IAT calls: `call/jmp dword ptr [slot]` → the import's thunk `I_<slot>`; any other indirect
    call/jump → `rc32_icall`.
  - `FS:`-relative operands → `RC_M(c->fs + ...)`, the per-thread TEB.
  - the direction flag (`df`), `pushad`/`popad`, `pushfd`/`popfd` (carrying the EFLAGS bits
    outside the modelled ones in `eflags_other`, so the CPUID-availability idiom that toggles the
    ID flag works), `enter`/`leave`, `loop`/`loope`/`loopne`/`jecxz`, `rdtsc`, `xlat`, segment
    register reads as flat selectors.
- **`xlat/emit_x87.py`** — x87 and MMX, decoded from the escape opcode + ModRM (Capstone's
  register-form operand lists vary; the encoding does not). The register stack is doubles in
  `rc_ctx.fpu` (`runtime/rc32_x87.h`): `fld`/`fst(p)`/`fild`/`fistp`/`fbld`, arithmetic,
  `fcom*`/`fucom*`/`fcomi*`/`ftst`/`fxam`, `fnstsw`/`fnstcw`/`fldcw`, `fprem(1)`, `frndint`,
  `fscale`, `fxtract`, `fpatan`/`fyl2x`/`fsin`/… , `fldenv`/`fnstenv`/`fnsave`/`frstor`, the
  `fnstsw ax; test ah,..` and `_ftol2` (`fistp` with a round-to-zero control word) idioms. MMX
  maps onto the SSE helpers of `rc_sse.h`, with an `emms`/`femms` tag reset.
- **`xlat/pe.py`** — a PE32 reader: sections, base relocations, imports (IAT → `dll!name`),
  exports, the TLS directory, the SafeSEH handler table.
- **`build/gen_image_pe.py`** — whole-image generator, sharing `gen_image.py`'s chunker and ninja
  writer. Output: `part_*.c` (the functions), `table.c` (`rc32_table`), `imports.c` (one thunk
  per IAT slot + `rc32_imports`), `decls.h`, `build.ninja` (`libgame_rc.a`), `manifest.json`
  (per function: status, instruction count, callee-cleaned bytes, inferred convention; discovery
  and coverage totals).
- **`runtime/rc32_*.{c,h}`** — the runtime additions, all behind `-DRC_GUEST32`:
  - `rc32_mem.c`: one 4 GB window reserved `PROT_NONE` at start, committed in pieces; the first
    64 KB left unmapped so a null pointer faults; a bump arena for guest stacks/TEBs/heap.
  - `rc32_image.c`: map the PE at its preferred base, fill the IAT (each slot → a one-word cell
    in a reserved area, so a stored-then-called import pointer still dispatches), the static-TLS
    template, `rc32_icall` (translated function, import thunk, or a live SEH resume point).
  - `rc32_teb.c`: per-thread context, guest stack and TEB inside the window, and `rc32_call`
    (native → guest with stack args, ecx/edx for thiscall/fastcall).
  - `rc32_seh.c`: a frame-based SEH dispatcher — the `FS:[0]` chain, `RaiseException`,
    `RtlUnwind`, and a resume mechanism (a function with resume points `setjmp`s at entry; an
    indirect jump to one `longjmp`s there, dropping the host frames in between, as the CRT's
    handlers transfer control to a `__except`/catch).
  - `rc32_imports.c`: the default `rc32_import_call` — the runtime's own `RaiseException`,
    `RtlUnwind`, `Tls*`, `GetLastError`/`SetLastError`, thread/process id; every other Win32
    import fails with its name (the Win32 seams are P3 work, not the front end).
  - `rc32_x87.h`, `rc32_sse.h`, `rc32_gen.h`: the x87/MMX model, `pmaddubsw`, and the generated
    code's register-fill/spill prelude.
- **`rc_ctx`** gains an `fpu` (x87 stack as doubles, control/status words, TOP, tag, MMX), an
  `fs` (TEB base) and a `ret_to` (the last `ret` target), all behind `RC_GUEST32`. The generated
  64-bit output is byte-for-byte unchanged (verified: the whole Mac image regenerates identically).

## Coverage

Whole-DLL translation compiles for arm64 at `-O2` and links against the runtime with stub seams.
Function discovery (PE has no function-starts table) seeds from the entry, exports, TLS callbacks
and SafeSEH handlers, then follows direct calls, relocated code pointers and the gaps between
functions, cutting at padding and rejecting pieces that decode to instructions 32-bit MSVC code
never emits (`daa`, `lds`, `bound`, …).

| DLL | starts | translated (ok) | stubs | instrs | imports | conventions (cdecl/this/std/fast) |
|---|---|---|---|---|---|---|
| `steam_api.dll` | 1521 | 1516 | 5 | 50,093 | 82 | 1221 / 250 / 34 / 11 |
| `bink2w32.dll` | 605 | 604 | 1 | 78,429 | 103 | 273 / 109 / 161 / 61 |
| `systemdetection.dll` | 2055 | 2051 | 4 | 103,785 | 148 | 1040 / 930 / 57 / 24 |

(`starts` is every discovered function start, including gap pieces; the whole-image generator
emits a trap stub for the few it cannot translate — those are counted but not tested.)

Instructions not handled (the untranslated functions, from `manifest.json`):

- **AVX / AVX2** CRT string paths in `steam_api` (`vpcmpeqb/vpcmpeqw ymm`, `vpmovmskb`,
  `vzeroupper`) and `pcmpistri` — the reported CPUID never advertises AVX or SSE4.2, so the guest
  CRT selects its SSE2 variant and these are dead. (3 functions.)
- **`pmaddubsw` on xmm** — handled; no longer a blocker.
- A handful of **gap pieces** that decode to one-byte `daa`/`lds` etc.: data between functions
  (jump tables, strings) that the sweep tried and correctly rejected; not real functions.
- A few **`callee is not a function start`** stubs: a direct call whose target is inside another
  function's body (tail-shared code); generated as a trap, which no tested path reaches.

## Differential test

`test/x86/difftest.py` runs each translated function and the same original bytes under Unicorn
from the same seeded state (registers, a stack with a return sentinel and arguments, a scratch
region part-filled with pointers into itself, a TEB, x87/MMX/SSE state) and compares the end
state: status (return / first import call and its index / abnormal / timeout), the eight GPRs,
xmm0–7, the x87 stack (TOP, which registers are empty, the live values, C0 C2 C3 after a compare,
the control word), the MMX registers when the function uses MMX, and the bytes of the stack, the
scratch, the TEB and the image's writable sections. DLLs are read from the command line or
`RC32_TEST_DLLS` and never copied; with no image the test skips.

`test/x86/handcases.py` adds hand-written snippets (x87 idioms, flag corners, the four
conventions, string ops with the direction flag, MMX, SSE, loops) assembled into a throwaway PE,
so the x87/flag/convention paths are exercised without any DLL.

Result (6 seeded cases per function, plus the hand snippets at up to 40):

| suite | tested | mismatches | notes |
|---|---|---|---|
| `steam_api.dll` | 1516 functions, ~9,100 cases | **0** | — |
| `bink2w32.dll` | 598 functions, ~3,590 cases | **0** | — |
| `systemdetection.dll` | 2054 functions, ~12,300 cases | 5 functions | the CPUID-parser cluster (below) |
| hand snippets | 23 snippets, 920 cases | **0** | 891 return-matches, 3 `reference_limit` |

(Besides the matches, many cases end `both_abnormal` — the seeded pointer argument is not a valid
object, so both sides fault identically; that is a pass. A few hit Unicorn's instruction limit
and are counted `timeout`, not compared.)

Both sides are reconciled where the reference is not hardware-faithful or the model is
deliberately looser (each handled in the comparator, not by changing the translation):

- **EFLAGS across a call** — x86 keeps flags live across a call; the translator models the
  arithmetic flags as function-local C variables (as the 64-bit build does — exact for
  compiler-generated code, which never reads flags defined before a call after it). A `pushfd`
  inside a callee therefore spills a different flags image to *already-popped* stack. The
  comparator ignores stack below the final esp (unallocated).
- **AF, and IF/ID and the other non-modelled EFLAGS bits** — AF is not modelled; the comparator
  masks it out of `pushfd` images, and the runtime round-trips the rest through `popfd`/`pushfd`
  (so the CPUID-availability toggle works).
- **x87 C0/C2/C3** — defined only by a compare; x87 arithmetic leaves them undefined, so they are
  compared only when the function ran an `fcom`/`fucom`/`ftst`/`fxam`/`ficom`.
- **x87 80-bit exponent range** — the model computes the stack as `double`; a value whose 15-bit
  x87 exponent overflows `double` reads as inf. The comparator treats inf-vs-finite (same sign)
  in a float result as this known range limit. NaN payloads (implementation-defined) are treated
  as equal.
- **`fprem`/`fprem1` partial reduction, `fyl2x`/`fsqrt` of out-of-domain inputs** — x87 does a
  partial reduction (C2 = 1) whose step is the reference's own, and QEMU's values for a negative
  `fsqrt`/`fyl2x` operand are not hardware's; these cases are marked `reference_limit`, not a
  mismatch.

### Known gaps

- **5 functions in `systemdetection`** (`0x10010880`, `0x10010c50`, `0x10010d10`, `0x1001fd9d`,
  `0x1002d290`), one `0x10010xxx` cluster: the CPUID brand/feature parser, built from
  nested/overlapping computed jump tables where table data sits in the code and the index is
  pre-biased (`and eax,3; add ecx,eax; jmp [ecx*4+T]`). Discovery decodes some of that table data
  as instructions, so an inner indirect jump is left unresolved and traps at run time; one
  (`0x10010c50`) diverges only by one dead flags-image byte a sub-call spilled (the EFLAGS point
  above). They are not on the game's path and will be pinned by the per-target name map / an
  override for the game image (below). Everything else in the three DLLs matches.

## What is left for the game image (P3+)

The front end is complete for the plain DLLs. The game's own `Rocksmith2014.exe`, once unpacked
to its original entry point, adds:

- **Virtualised functions** — code VMProtect leaves as bytecode (licence / integrity checks).
  They are found by their jumps into the VMProtect sections and replaced by native overrides
  (normally "check passed"); the guest window holds the original unpacked bytes, so runtime
  self-checksums pass. None of the three plain DLLs is virtualised, so this is untested here.
- **CRT specifics** — the exe's statically-linked MSVC CRT: its global constructors and
  `_initterm` tables, `__security_init_cookie` (the `/GS` cookie, seeded from `rdtsc`/`GetTickCount`
  — made deterministic for the recompile), the `_onexit`/`atexit` table, `_CxxThrowException` →
  `__CxxFrameHandler3` C++ EH (the frame handler is translated like any function; the catch
  continuation is a resume point, already handled by `rc32_seh.c`), and the AVX2/SSE4.2 string
  routines the reported CPUID keeps dead.
- **The Win32 / D3D9 / Steam / Uplay / Cg seams** — every import beyond the handful the runtime
  implements. `rc32_imports.c` traps the rest by name today; P3 fills them from
  `shims/win32`, the Steam/Uplay faces and `shims/d3d9`.
- **Per-target tables** — the exe's `win-x86` target under `tools/targets/` (the name-keyed
  `symbols.map` / `offsets.map` the parallel refactor introduces): the convention and
  callee-cleanup of each named function (inferred here and recorded in `manifest.json`), the
  override and hook points, and the handful of functions whose discovery or jump tables need a
  pinned entry (the five `systemdetection` cases are the template for that).

### Mapping `manifest.json` → `symbols.map`

The front end does **not** write `symbols.map` directly; the inference lands in a separate JSON
schema, so the win-x86 target assembly reads it mechanically. `manifest.json` has, per function,
`per_function["<addr-hex>"] = [status, reason, ninsn, chunk, conv]` where `conv` is
`{"pops": <callee-cleaned bytes from ret N>, "conv": "cdecl|stdcall|thiscall|fastcall"}` (or
`null` for a stub). The `symbols.map` row `NAME  ADDRESS  CONVENTION  PROTOTYPE` is built from it:

- **ADDRESS** — the `per_function` key, an absolute VA at the preferred image base (not an RVA).
- **CONVENTION** — `conv["conv"]`; `conv["pops"]` is the exact callee-cleanup in bytes
  (`4 * args` for stdcall/thiscall/fastcall, `0` for cdecl), kept because it is observed from the
  `ret N`, not derived.
- **NAME** — the PE export name where the function is exported (`xlat/pe.Image.exports`, which the
  generator already uses for the C comment); otherwise synthesise `F_<addr>`.
- **PROTOTYPE** — *not* inferred. The front end recovers the convention and arg-area size but not
  argument types; the prototype is still the maintainer's / the thunk signatures' to fill.

The convention is an inference (reads the first ~64 instructions for ecx/edx use and the `ret`
form), so treat it as a default the target may override, not ground truth — the same status it
has in P0's `symbols.map`.

## Running it

```
# whole-image translate + compile (arm64) one DLL
venv/bin/python -I build/gen_image_pe.py <IMAGE.dll> <OUTDIR> --jobs 8 --cflags=-w
ninja -C <OUTDIR>

# differential test (needs a Python with capstone + unicorn; UC_IGNORE_REG_BREAK=1 quiets a warning)
UC_IGNORE_REG_BREAK=1 venv/bin/python -m test.x86.difftest <IMAGE.dll> --out <DIR> --cases 6 --jobs 8
UC_IGNORE_REG_BREAK=1 venv/bin/python -m test.x86.handcases --out <DIR>          # no DLL needed
```

The DLLs are build inputs only: never committed, nor anything derived from them.
