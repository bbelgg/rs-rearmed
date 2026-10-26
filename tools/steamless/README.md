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

## Tools

### 1. `dump-decrypted.sh`

Dumps the decrypted `__text` from a running game process using lldb.

```bash
# Launch game through Steam, wait for main menu, then:
./tools/steamless/dump-decrypted.sh [output_dir]
```

### 2. `patch-binary.py`

Reconstructs a DRM-free x86_64 Mach-O from the dump.

```bash
python3 tools/steamless/patch-binary.py [--dump PATH] [--output PATH]
```

Steps:
1. Extracts the x86_64 thin slice from the fat binary
2. Replaces `__text` with the decrypted dump
3. Fixes `LC_UNIXTHREAD` entry point if needed
4. Re-signs with ad-hoc codesign
5. Verifies the result disassembles correctly

## Workflow

```
1. Launch Rocksmith through Steam → wait for main menu
2. ./tools/steamless/dump-decrypted.sh
3. python3 tools/steamless/patch-binary.py
```

The decrypted binary at `build/steamless/Rocksmith2014_x64_decrypted`
is gitignored.
