# Steamless tools

Tools to obtain a decrypted Rocksmith 2014 binary.

## The DRM Scheme

Rocksmith 2014's macOS binary uses **Valve SteamStub DRM** (macOS variant):

1. **Stub header** at the start of `__text` (magic `eojx{`, 288 bytes): contains
   symbol names for the runtime loader (`_NSCreateObjectFileImageFromMemory`,
   `_NSLinkModule`, `_NSLookupSymbolInModule`, `_ptrace`, `_vm_allocate`, etc.)
   and offsets into the stub region.

2. **Stub entry code** (12 KB at `__text + 0x120`): x86_64 code that runs as the
   process entry point. It:
   - Calls `_ptrace(PT_DENY_ATTACH)` to block debuggers
   - Resolves dyld symbols via `__dyld_lookup_and_bind`
   - Allocates memory with `_vm_allocate`
   - Loads `drm_bundle.dylib` (1.4 MB, embedded in `__text`) via
     `NSCreateObjectFileImageFromMemory` + `NSLinkModule`
   - Calls the bundle's entry which contacts the Steam client, verifies
     ownership, and decrypts `__text` in place
   - Jumps to the real entry point (same address — the stub overwrites itself)

3. **DRM bundle** (`drm_bundle.dylib`, ~1.4 MB Mach-O bundle embedded in the
   stub region): handles the actual Steam client communication and decryption.
   Uses `_vm_allocate`/`_vm_deallocate` for memory management.

4. **Encrypted payload** (`text_xor.bin`, 20,850,689 bytes = full `__text`):
   the on-disk `__text` content. After the DRM bundle decrypts it, the entire
   region becomes valid x86_64 code.

### Key addresses (x86_64 slice)

| Item | Address/Offset |
|---|---|
| `__text` section start | `0x100001B00` (file offset 6912 in thin binary) |
| `__text` size | `0x13E2801` (20,850,689 bytes) |
| LC_UNIXTHREAD rip | `0x100001B00` (points into the stub / real entry) |
| Stub header | `__text + 0x00` (288 bytes, magic `eojx{`) |
| Stub entry code | `__text + 0x120` (12 KB) |
| DRM bundle | `__text + 0x3120` (~1.4 MB) |
| Fat binary x86_64 offset | 26,312,704 |

On disk `__text` is the code XORed with a repeating 8-byte key, the same key from the first byte to the last; the
rest of the file is as it is. So the file can be decrypted without running anything.

## `decrypt.py`

```bash
python3 tools/steamless/decrypt.py [--game DIR] [--output FILE]
```

Writes `build/steamless/Rocksmith2014_x64_decrypted`: the x86-64 slice with `__text` in the clear. No game
launch, no Rosetta, no Steam account: your Steam copy is only read.

It finds the key in your own binary, nothing ships: the key that turns the top of `__text` into the C runtime's
start must also turn its most common aligned block into the compilers' 8-byte NOP. It never prints the key. The
result is checked against the pins in `tools/targets/mac-x64/target.toml`: a binary that isn't the game, or another
build of it, is refused in plain words and nothing is written. A Steam copy with a mod's extra load commands
has the pinned `__text` and another header: it is used, and says so. The output for the clean depot binary
matches `image_sha256` byte for byte. `test_decrypt.py` tests it on a made-up Mach-O.

The binary at `build/steamless/Rocksmith2014_x64_decrypted` is gitignored.

Earlier versions dumped `__text` from a game started through Steam, with a dylib added to the Steam copy for the
moment; that needed Rosetta and the owning account. The XOR of the file reproduces that dump with 0 differing bytes.
