# Steamless tools

`decrypt.py` makes the decrypted Rocksmith 2014 binary every Mac build starts from.

## The DRM Scheme

Rocksmith 2014's macOS binary uses **Valve SteamStub DRM** (macOS variant). In its x86-64 slice:

- `__text` is the code XORed with a repeating 8-byte key, the same key from the first byte to the last. In the clear
  it starts with the C runtime's start, and no Mach-O bundle is embedded in it.
- The last 288 bytes of the slice, the end of `__LINKEDIT`, are a block with the magic `eojx{` and the names of what
  a loader needs (`_NSCreateObjectFileImageFromMemory`, `_NSLinkModule`, `_NSLookupSymbolInModule`, `_ptrace`,
  `_vm_allocate` and others).
- The rest of the file is as it is. So the file can be decrypted without running anything.

### Key addresses (x86_64 slice)

| Item | Address/Offset |
|---|---|
| `__text` section start | `0x100001B00` (file offset 6912 in thin binary) |
| `__text` size | `0x13E2801` (20,850,689 bytes) |
| LC_UNIXTHREAD rip | `0x100001B00` (the start of `__text`) |
| `eojx{` block | the last 288 bytes of the slice |
| Fat binary x86_64 offset | 26,312,704 |

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
