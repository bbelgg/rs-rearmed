#!/usr/bin/env python3
"""
Reconstruct a DRM-free Rocksmith 2014 x86_64 Mach-O binary.

Takes the decrypted __text dump (from dump-decrypted.sh) and patches it into
a thin x86_64 copy of the game binary. The DRM stub is NOPed out and the
entry point is fixed to jump to the real code.

Usage:
    python3 patch-binary.py [--dump PATH] [--output PATH]
"""
import argparse
import os
import struct
import subprocess
import sys
import shutil

GAME_DIR = os.environ.get("RS_GAME_DIR") or os.path.expanduser(
    "~/Library/Application Support/Steam/steamapps/common/Rocksmith2014"
)
GAME_BIN = os.path.join(GAME_DIR, "Rocksmith2014.app/Contents/MacOS/Rocksmith2014")
STORAGE = "build/steamless"

# __text section in the x86_64 slice
TEXT_FILE_OFFSET = 6912          # offset within the x86_64 Mach-O (from otool -l)
TEXT_SIZE = 0x13E2801            # 20850689 bytes
TEXT_VMADDR = 0x100001B00

# The x86_64 slice in the fat binary
FAT_X64_OFFSET = 26312704
FAT_X64_SIZE = 28544152

# DRM stub header (appears at the start of __text on disk)
STUB_MAGIC = b"eojx{"

# Real entry point: after decryption, __text starts with a standard C entry
# push %rbp; mov %rsp, %rbp or similar.
# The LC_UNIXTHREAD rip points to TEXT_VMADDR (0x100001b00) which is both
# the stub and the real entry — after decryption the stub IS the real code.


def extract_x64_slice(fat_path: str, out_path: str) -> None:
    """Extract the x86_64 slice from the universal binary."""
    subprocess.check_call(["lipo", fat_path, "-thin", "x86_64", "-output", out_path])


def find_real_entry(decrypted: bytes) -> int:
    """Find the offset within decrypted __text where real code begins.

    The DRM stub occupies the first ~1.5 MB of __text. After decryption
    the stub area contains the decrypted real code starting at offset 0.
    If the dump was taken from memory after decryption, the entire region
    is real code and the entry is at offset 0 (= TEXT_VMADDR).
    """
    # Check if the first bytes look like valid x86_64 code
    # push rbp = 0x55, push r12-r15 = 0x41 5x, xor reg,reg = 0x31/0x33
    # sub rsp = 0x48 83 ec, jmp = 0xe9/0xeb
    first = decrypted[0]
    valid_starts = {0x55, 0x41, 0x48, 0x31, 0x33, 0x50, 0x53, 0x56, 0x57,
                    0xe9, 0xeb, 0x6a, 0x89, 0xb8, 0xbf, 0xbe, 0xba, 0xcc}
    if first in valid_starts:
        return 0

    # The probe.bin from earlier analysis (the stub header) is NOT decrypted code.
    # If we see the stub magic, the dump is wrong.
    if decrypted[:5] == STUB_MAGIC:
        print("ERROR: Dump still contains the DRM stub header, not decrypted code.", file=sys.stderr)
        sys.exit(1)

    # Search for a plausible entry signature
    for off in range(0, min(len(decrypted), 0x200000), 16):
        if decrypted[off] in valid_starts:
            return off

    return 0


def patch_lc_unixthread(data: bytearray, new_rip: int) -> bytearray:
    """Find LC_UNIXTHREAD and set rip to new_rip."""
    LC_UNIXTHREAD = 0x5
    off = 0
    # Skip Mach-O header: magic(4) + cputype(4) + cpusubtype(4) + filetype(4)
    #                      + ncmds(4) + sizeofcmds(4) + flags(4) + reserved(4) = 32
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off = 32  # past mach_header_64

    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == LC_UNIXTHREAD:
            # x86_64 thread state: flavor(4) + count(4) + 21 uint64s
            # rip is the 17th register (index 16, 0-based) after flavor+count
            rip_off = off + 8 + 8 + 16 * 8  # cmd+cmdsize + flavor+count + 16 regs
            struct.pack_into("<Q", data, rip_off, new_rip)
            return data
        off += cmdsize

    print("WARNING: LC_UNIXTHREAD not found, entry point not patched.", file=sys.stderr)
    return data


def main():
    parser = argparse.ArgumentParser(description="Patch Rocksmith 2014 with decrypted __text")
    parser.add_argument("--dump", default=os.path.join(STORAGE, "text_decrypted.bin"),
                        help="Path to decrypted __text dump")
    parser.add_argument("--output", default=os.path.join(STORAGE, "Rocksmith2014_x64_decrypted"),
                        help="Output path for the patched binary")
    parser.add_argument("--game", default=GAME_BIN,
                        help="Path to the original game binary (fat)")
    args = parser.parse_args()

    if not os.path.isfile(args.dump):
        print(f"ERROR: Decrypted dump not found: {args.dump}")
        print("Run dump-decrypted.sh first while the game is running.")
        sys.exit(1)

    if not os.path.isfile(args.game):
        print(f"ERROR: Game binary not found: {args.game}")
        sys.exit(1)

    print("=== Rocksmith 2014 Binary Patcher ===")

    # Step 1: Extract x86_64 thin binary
    thin_path = args.output + ".tmp"
    print(f"Extracting x86_64 slice from: {args.game}")
    extract_x64_slice(args.game, thin_path)

    # Step 2: Read the decrypted __text
    print(f"Reading decrypted __text from: {args.dump}")
    with open(args.dump, "rb") as f:
        decrypted = f.read()

    if len(decrypted) != TEXT_SIZE:
        print(f"WARNING: Expected {TEXT_SIZE} bytes, got {len(decrypted)}")

    entry_off = find_real_entry(decrypted)
    real_rip = TEXT_VMADDR + entry_off
    print(f"Real entry point: 0x{real_rip:x} (offset {entry_off} in __text)")

    # Step 3: Patch the binary
    print("Patching __text in the thin binary...")
    with open(thin_path, "rb") as f:
        binary = bytearray(f.read())

    # Replace __text section content
    binary[TEXT_FILE_OFFSET:TEXT_FILE_OFFSET + len(decrypted)] = decrypted

    # Fix entry point if needed
    if entry_off != 0:
        print(f"Patching LC_UNIXTHREAD rip to 0x{real_rip:x}")
        binary = patch_lc_unixthread(binary, real_rip)

    # Write patched binary
    with open(args.output, "wb") as f:
        f.write(binary)
    os.chmod(args.output, 0o755)

    # Remove temp file
    os.unlink(thin_path)

    # The Steam build is unsigned — don't add a signature (it breaks Steam's launch).

    # Step 5: Verify
    print("\nVerification:")
    print(f"  Output: {args.output}")
    print(f"  Size: {os.path.getsize(args.output)} bytes")

    # Check first bytes of __text
    with open(args.output, "rb") as f:
        f.seek(TEXT_FILE_OFFSET)
        first = f.read(4)
    print(f"  __text first bytes: {first.hex()}")

    if first == b"\x11\xe1\xcf\x98":
        print("  ERROR: Still encrypted!")
        sys.exit(1)

    # Quick disassembly check
    try:
        result = subprocess.run(
            ["objdump", "-d", "--start-address=0x100001b00",
             "--stop-address=0x100001b20", args.output],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0 and result.stdout.strip():
            print("  Disassembly check: OK")
            for line in result.stdout.strip().split("\n")[-5:]:
                print(f"    {line}")
    except Exception:
        pass

    print(f"\nDone. Decrypted binary: {args.output}")
    print("Next: point the build at it (docs/BUILDING.md, Manual build, step 3):")
    print(f'  export RC_IMAGE="{os.path.abspath(args.output)}"')


if __name__ == "__main__":
    main()
