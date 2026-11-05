# Static-recompiling Rocksmith 2014 to Apple Silicon

The aim is a native arm64 app, produced by static recompilation from a Steam copy of the game, that boots,
plays a song and can load custom songs (CDLC). Nothing is emulated or translated at run time; every guest
instruction is turned into C ahead of time and compiled for arm64.

This file gives the shape of the work and the few facts that are expensive to rediscover. It gives no
addresses — you recover those from your own copy. Work out the routine engineering yourself.

## The two editions

| | Mac edition | Learn & Play |
|---|---|---|
| Binary | x86-64 Mach-O, System V ABI | 32-bit PE, MSVC conventions |
| Steam | app 221680, depot 221682 | app 221680, depot 221683 (a Windows-only license) |
| On-disk protection | Valve SteamStub, a repeating XOR | a commercial packer; real code exists only in memory |
| Graphics API | OpenGL 2.1 + Cg | Direct3D 9 |
| C/C++ libraries | macOS frameworks | Microsoft's DLLs, which you also translate |

Do whichever edition the account owns. If both, do the Mac first — it is far easier, and its results carry
over. Both editions are the same C++ source compiled twice, so once you have named functions in one, match
them in the other by strings, constants and call shape rather than reversing twice.

## 1. Get a clean image to translate

The translator needs the game's real code as a plain file. Each edition hides it differently.

**Mac.** The x86-64 slice's `__text` is the machine code XORed with a single repeating 8-byte key, the same
key from the first byte to the last; the rest of the file is already plain. You do not need to run anything —
recover the key from known plaintext. Two anchors pin all 8 bytes: the first bytes of `__text` must decrypt
to the C runtime's entry, and the most common aligned 8-byte block must decrypt to the compiler's 8-byte NOP
(`0f 1f 84 00 00 00 00 00`). Apply the key and you have the image. It is largely stripped, so class names,
RTTI and log strings are your naming material.

**Learn & Play.** The exe is packed: `.text`, `.rdata` and `.data` are empty on disk and only materialise in
memory as the game starts. This is the one step that needs a Windows environment, and the one input neither
this file nor the game can hand you. There are two routes; both read *your own* copy and keep its code on
your machine.

- *A Windows PC that runs the game.* Write a small 32-bit program that reads the running game from the
  outside with `ReadProcessMemory` — no debugger, no injection, so the anti-debugging never fires. Reach a
  song, then dump every committed page of the process and each loaded module's export table.

- *No Windows PC: capture and replay the unpacker (the "seed" route).* The packer's bootstrap decompresses
  the sections *before and independently of* its anti-debug check, and that decompression is a deterministic
  function of a small amount of its runtime state — identical output every run. So you capture that state
  once and replay it on the Mac. The captured state holds the unpacker's scratch and registers, not game
  code; it is tied to one exe build.
  1. Run 32-bit Windows in QEMU with a GDB stub (`-cpu max,hypervisor=off`, or Windows hangs at the boot
     manager). Install the VC++ 2010 runtime. Place the exe beside `steam_api.dll`, `bink2w32.dll`,
     `d3dx9_42.dll`.
  2. Launch the game; it halts at "Please make sure Steam is running". Leave it and read its image base. The
     base is randomised once per boot, but a second launch lands at the same base as the first.
  3. From the stub, set a hardware write watchpoint on `.text` (invisible to the guest) and launch again. It
     stops at the first write to `.text` — the moment before decompression.
  4. Save the unpacker's registers, GDT and scratch pages at that point. In Unicorn on the Mac, lay the exe
     out at its section addresses, apply the saved state, and map benign kernel32 stubs (the packer only
     checks one anti-hook byte per target; a `8b ff 55 8b ec` prologue satisfies it — no real DLL bytes). Run
     the decompression and dump the sections. Confirm two runs agree byte for byte.

Either route gives you the decompressed sections. Then rebuild a normal PE from them: the true section table,
`.data` as captured with the `.bss` tail zeroed, an import directory laid over the real IAT, entry point
`WinMainCRTStartup`, and the packer's own sections left non-executable. Two things to know:

- **Relocations.** The game's own relocation table stays packed. Recover it by comparing two copies loaded at
  different bases — the bytes that differ are the fixup sites. Without this the translator misses every
  function reached only through a pointer. Leave words that merely look like pointers untouched; rewriting
  them corrupts the song-signature key and no song will load.
- **Imports.** Most IAT slots are encrypted and thousands of call sites are redirected through single-use
  stubs. Run each stub as a black box over the image until it lands on a known export, and feed the
  translator a table of (call site, resume address, resolved import). No game function itself is virtualised,
  so this is the whole of what you must undo.

## 2. Translate the image

Generate C from the machine code and compile it for arm64. This is the bulk of the work.

- One C function per guest function, `F_<address>(ctx)`. The guest registers live in a context struct passed
  through; the arithmetic flags can be plain C locals, since compiler-emitted code never reads flags across a
  call (carry them explicitly only for the few hand-written helpers that return one).
- Direct calls become C calls. Indirect calls and jumps go through an address→function lookup table. Resolve
  jump tables at translation time.
- **Find the functions.** On the Mach-O, the `LC_FUNCTION_STARTS` table lists them outright. On the PE, seed
  from the entry point, exports, TLS callbacks and SafeSEH handlers, then follow direct calls and relocated
  code pointers and fill the gaps, rejecting runs that do not decode as real prologues.
- A function you cannot translate becomes a stub that aborts with its own address. Never emit code that is
  quietly wrong.
- **Fence memory accesses from the start.** x86 is strongly ordered (TSO) and arm64 is not. Emit a barrier
  around translated loads and stores (stack and read-only-data accesses excepted). Skip this and the compiler
  hoists loads out of the game's spin loops and threads read each other's writes stale — both appear as rare,
  baffling hangs, not as obvious bugs.
- Make generation deterministic and split the output into many translation units, so a 15–25-minute build can
  parallelise and resume.

The 32-bit front end needs more than the 64-bit one: the four MSVC calling conventions (inferred from
`ecx`/`edx` use and the `ret N` form), the x87 stack (computing in `double` matches MSVC's default precision),
and MMX. Report a CPU with SSE2 but no AVX or 3DNow!, and the game's wide-vector and 3DNow! paths stay dead.

**The one tool worth building before you trust any output:** a differential harness that runs each translated
function and the original bytes from the same random start state (the original under an x86 emulator such as
Unicorn, or under Rosetta on the Mac) and compares the resulting registers and memory. A translation bug
otherwise surfaces as a crash far from its cause.

## 3. Give the translated code a world to run in

Common to both editions: a per-thread context, trampolines so native callbacks can enter translated code,
`setjmp`/`longjmp` across guest frames, varargs, and an import thunk per external symbol generated from its
prototype.

**Mac.** Map the original image for its data and run your translated functions in place of its code; bind its
imports to the real arm64 frameworks through the thunks. A few imports have no arm64 form and must be
supplied yourself: Cg, `libsteam_api`, Bink, and a link stub for `libstdc++.6`. Note that Objective-C
struct-returning message sends use a different ABI on arm64 than on x86-64. Replace the game's Cocoa shell
(window, display link, input, paths) with native code that calls the game's bootstrap and per-frame entry
points.

**Learn & Play.** arm64 macOS leaves the low 4 GB unmapped, so reserve one 4 GB window and address all guest
memory as `base + (uint32_t)addr`; everything the guest points at lives inside it. Then:

- Translate the DLLs the game loads (msvcr100, msvcp100, d3dx9_42, D3DCompiler_42, bink2w32) rather than
  reimplementing them — the game relies on their exact data layouts. Take Microsoft's from the user's own Steam
  redistributables or Microsoft's installers, and `bink2w32.dll` from the game folder. Load each at a fixed
  base, rebasing any that collide, and run `DllMain` in dependency order.
- Implement the slice of Win32 the game and those DLLs actually call (kernel32, user32, gdi32, ole32,
  dinput8, avrt and a handful more) on POSIX and Cocoa, with unknown imports routed to a by-name dispatcher
  that logs and fails. Report Windows 6.x. COM objects are guest vtables of host function cells. The game
  quits without a DirectInput keyboard, so wire input early.
- Provide SEH: a frame-based dispatcher over the `FS:[0]` chain. The translated CRT does the C++ exception
  machinery itself, but your translator must recognise `__except` blocks and catch continuations and turn
  them into resume points.

## 4. Draw, and get to a picture

**Mac.** Apple's OpenGL still runs on arm64 — use it first and leave Metal for later. Translate the game's Cg
(ARB) programs to GLSL.

**Learn & Play.** Implement the `IDirect3DDevice9` subset the game uses on top of a GL-style layer: translate
SM2/SM3 shader bytecode to GLSL, and generate programs for the fixed-function pipeline, which Gamebryo uses
heavily. The usual D3D9↔GL conventions apply (half-pixel offset, depth range, constant-matrix order, vertex
colour order).

Audio can come up quietly: on the Mac the game's Wwise sink is CoreAudio already; on Learn & Play its
PortAudio/WASAPI path needs CoreAudio-backed COM objects underneath. No guitar is required to reach gameplay —
the tuner can be skipped. If you suppress the intro movie to boot faster, still load its model and let the
boot sequence finish its steps, or the title screen never arrives.

## 5. Make it run offline

Several boot-time checks expect a signed-in client and a reachable network; satisfy them locally so the app
runs fully offline:

- A Steam layer that never loads Valve's library: it reports the identity from the local `loginusers.vdf`,
  the base game and installed DLC (and AppID 248750, which custom songs carry) as owned, keeps cloud saves in
  a local folder, and serves stats locally. On Learn & Play it is an MSVC `thiscall` interface whose vtable
  shape you read from the game's own `steam_api.dll`.
- A stand-in for the game's account/online layer that returns the same answers the game falls back to when a
  network is unavailable, so the title screen proceeds straight to the profile list.

## 6. Load custom songs

A CDLC archive is a `.psarc` placed in the game's `dlc/` folder (named `*_m.psarc` for Mac, `*_p.psarc` for
Learn & Play). Four gates keep a custom song out; open all four:

1. **Ownership** — the game mounts an archive only if the AppID in its `appid.appid` entry is DLC your Steam
   layer reports installed. List every present archive's AppID, or answer yes while the mount runs.
2. **Signature** — the song (SNG) loader calls a DSA verifier (data, length, hex key); make that call succeed.
   Bypass it only at that call site; the verifier has other, legitimate callers.
3. **Song-key table (Learn & Play only)** — the song list keeps an entry only if the CRC-32 of its `SongKey`
   is in a table baked into the image; make that lookup return true.
4. **Open-file limit** — raise it at startup. A GUI-launched process gets 256 descriptors, and a large
   library needs far more.

If no custom song is at hand, repack one of the game's own songs under a new AppID (and, for Learn & Play, a
new song key). The community's psarc tooling documents the format.

---

That is the whole path: a plain image, an ahead-of-time translator checked against the original, a runtime
and platform for the translated code to live in, a renderer, offline stand-ins, and the four CDLC gates. The
engineering is large but ordinary; the facts above are the ones that are slow to find by trial.
