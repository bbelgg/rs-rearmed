#!/usr/bin/env python3
"""
Add an LC_LOAD_WEAK_DYLIB load command to a Mach-O binary's x86_64 slice.
Same technique the community mod installers use.

Usage: python3 add_weak_dylib.py <binary> <dylib_path>
"""
import struct
import sys
import os

LC_LOAD_WEAK_DYLIB = 0x80000018
MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
CPU_TYPE_X86_64 = 0x01000007


def align(n, a):
    return (n + a - 1) & ~(a - 1)


def find_x64_slice(data):
    """Return (offset, size) of the x86_64 slice, or (0, len) for thin."""
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic == FAT_MAGIC:
        nfat = struct.unpack_from(">I", data, 4)[0]
        for i in range(nfat):
            off = 8 + i * 20
            cputype, _, slice_off, slice_size, _ = struct.unpack_from(">IIIII", data, off)
            if cputype == CPU_TYPE_X86_64:
                return slice_off, slice_size
        raise ValueError("No x86_64 slice found")
    elif struct.unpack_from("<I", data, 0)[0] == MH_MAGIC_64:
        return 0, len(data)
    else:
        raise ValueError(f"Not a Mach-O: magic {magic:#x}")


def has_load_command(data, base, name_bytes):
    """Check if a load command for this dylib already exists."""
    ncmds = struct.unpack_from("<I", data, base + 16)[0]
    off = base + 32  # past mach_header_64
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd in (0xC, 0xD, LC_LOAD_WEAK_DYLIB):  # LC_LOAD_DYLIB, LC_ID_DYLIB
            str_off = struct.unpack_from("<I", data, off + 8)[0]
            end = data.find(b'\x00', off + str_off)
            existing = data[off + str_off:end]
            if existing == name_bytes:
                return True
        off += cmdsize
    return False


def add_weak_dylib(path, dylib_name):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    base, slice_size = find_x64_slice(data)
    name_bytes = dylib_name.encode("utf-8")

    if has_load_command(data, base, name_bytes):
        print(f"Already has load command for {dylib_name}")
        return

    # Build the LC_LOAD_WEAK_DYLIB command
    # struct dylib_command { uint32 cmd, cmdsize; struct dylib { uint32 offset, timestamp; uint32 current_version, compat_version; }; char name[]; }
    str_offset = 24  # name starts after the fixed fields
    name_with_null = name_bytes + b'\x00'
    cmdsize = align(str_offset + len(name_with_null), 8)

    lc = struct.pack("<II", LC_LOAD_WEAK_DYLIB, cmdsize)
    lc += struct.pack("<I", str_offset)  # name offset
    lc += struct.pack("<I", 2)           # timestamp
    lc += struct.pack("<I", 0x10000)     # current_version 1.0.0
    lc += struct.pack("<I", 0)           # compat_version
    lc += name_with_null
    lc += b'\x00' * (cmdsize - len(lc))

    # Find where to insert: after the last existing load command
    ncmds = struct.unpack_from("<I", data, base + 16)[0]
    sizeofcmds = struct.unpack_from("<I", data, base + 20)[0]
    insert_off = base + 32 + sizeofcmds

    # Check there's padding space (Mach-O headers have padding before __text)
    # The __text section starts at file offset 6912 (0x1B00) in the thin binary,
    # or base + 6912 in the fat binary.
    text_start = base + 6912
    available = text_start - insert_off
    if available < cmdsize:
        print(f"ERROR: Not enough header space. Need {cmdsize}, have {available}.")
        print("The binary's header padding is too small for another load command.")
        sys.exit(1)

    # Insert the load command (overwrite padding zeros)
    data[insert_off:insert_off + cmdsize] = lc

    # Update ncmds and sizeofcmds in the header
    struct.pack_into("<I", data, base + 16, ncmds + 1)
    struct.pack_into("<I", data, base + 20, sizeofcmds + cmdsize)

    # Write back (new inode to avoid dyld signature cache issues)
    tmp = path + ".patching"
    with open(tmp, "wb") as f:
        f.write(data)
    os.rename(tmp, path)
    os.chmod(path, 0o755)

    print(f"Added LC_LOAD_WEAK_DYLIB for {dylib_name} ({cmdsize} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <binary> <dylib_path>")
        sys.exit(1)
    add_weak_dylib(sys.argv[1], sys.argv[2])
