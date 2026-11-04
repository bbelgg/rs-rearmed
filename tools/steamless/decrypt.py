#!/usr/bin/env python3
"""The Mac game's decrypted image, from your own game's binary, with nothing running.

    decrypt.py [--game DIR] [--output FILE]

The Steam binary's x86-64 __text is XORed with a repeating 8-byte key, and everything else in the slice is as it
is. The key is not kept anywhere here: it comes out of your own binary, from two things every such binary has at
known places, the C runtime's start at the top of __text and the compilers' 8-byte NOP as its most common aligned
block. The two have to agree. The result is the x86-64 slice with __text in the clear, a plain Mach-O: the
recompiler's input (tools/targets/mac-x64/target.toml pins it).

--game: the game folder, or the binary (default $RS_GAME_DIR, else Steam's default library). --output: default
build/steamless/Rocksmith2014_x64_decrypted. A copy with extra load commands from a mod has the pinned __text
and another header: it is used, and says so. Anything else is refused, and nothing is written.

Standard library only. Exit status: 0 done, 1 not this game, 2 another build of it.
"""
import argparse
import collections
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "targets"))
from targets import parse_toml  # noqa: E402

TARGET = os.path.join(ROOT, "tools", "targets", "mac-x64", "target.toml")
BINARY = os.path.join("Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014")
DEFAULT_OUTPUT = os.path.join("build", "steamless", "Rocksmith2014_x64_decrypted")
CPU_X86_64 = 0x01000007
FAT_MAGIC, MH_MAGIC_64, LC_SEGMENT_64 = 0xcafebabe, 0xfeedfacf, 0x19
CRT_START = bytes.fromhex("6a004889e54883e4")  # pushq $0; movq %rsp, %rbp; andq $-16, %rsp
PADDING = bytes.fromhex("0f1f840000000000")    # nopl 0(%rax,%rax)


class Failure(Exception):
    def __init__(self, msg, code=1):
        Exception.__init__(self, msg)
        self.code = code


def xor(a, b):
    return (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(len(a), "little")


def x86_64_slice(data):
    """The x86-64 Mach-O in a universal or thin binary."""
    if len(data) >= 8 and struct.unpack_from(">I", data)[0] == FAT_MAGIC:
        for i in range(struct.unpack_from(">I", data, 4)[0]):
            cpu, _, offset, size, _ = struct.unpack_from(">5I", data, 8 + 20 * i)
            if cpu == CPU_X86_64:
                return data[offset:offset + size]
        return None
    if len(data) >= 8 and struct.unpack_from("<II", data) == (MH_MAGIC_64, CPU_X86_64):
        return data
    return None


def text_section(macho):
    """(file offset, size) of __TEXT,__text, or None."""
    at = 32
    try:
        for _ in range(struct.unpack_from("<I", macho, 16)[0]):
            cmd, size = struct.unpack_from("<II", macho, at)
            if cmd == LC_SEGMENT_64 and macho[at + 8:at + 24].rstrip(b"\0") == b"__TEXT":
                for s in range(struct.unpack_from("<I", macho, at + 64)[0]):
                    sect = at + 72 + 80 * s
                    if macho[sect:sect + 16].rstrip(b"\0") == b"__text":
                        length, offset = struct.unpack_from("<QI", macho, sect + 40)
                        return offset, length
            at += size
    except struct.error:
        pass
    return None


def find_key(text):
    """The key, from the top of __text and from its most common aligned block; None when they disagree."""
    blocks = collections.Counter(text[i:i + 8] for i in range(0, len(text) - 7, 8))
    if not blocks:
        return None
    key = xor(text[:8], CRT_START)
    return key if xor(blocks.most_common(1)[0][0], PADDING) == key else None


def decrypt(data):
    """The decrypted slice and its __text's place; Failure when this is not the Mac game's binary."""
    macho = x86_64_slice(data)
    if macho is None:
        raise Failure("this isn't the Mac game's program: it has no Intel (x86-64) code")
    place = text_section(macho)
    if place is None or place[0] + place[1] > len(macho):
        raise Failure("this isn't the Mac game's program: it has no code section")
    offset, size = place
    text = macho[offset:offset + size]
    key = find_key(text)
    if key is None:
        raise Failure("this isn't a copy of the Mac game that RS Re-ARMed recognises: its code doesn't decrypt. In "
                      "Steam, right-click the game, choose Properties, Installed Files, Verify integrity of game "
                      "files, then try again")
    clear = xor(text, (key * (size // 8 + 1))[:size])
    return macho[:offset] + clear + macho[offset + size:], clear


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default=os.environ.get("RS_GAME_DIR") or os.path.expanduser(
        "~/Library/Application Support/Steam/steamapps/common/Rocksmith2014"))
    ap.add_argument("--output", default=DEFAULT_OUTPUT)
    ap.add_argument("--target", default=TARGET, help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    binary = os.path.join(a.game, BINARY) if os.path.isdir(a.game) else a.game
    pins = parse_toml(a.target)
    try:
        try:
            with open(binary, "rb") as f:
                data = f.read()
        except OSError:
            raise Failure("the game's program is not at %s" % binary)
        image, text = decrypt(data)
        if hashlib.sha256(text).hexdigest() != pins["text_sha256"]:
            raise Failure("this copy of the game is a different build from the one RS Re-ARMed is made for, so it "
                          "can't be used. In Steam, right-click the game, choose Properties, Installed Files, Verify "
                          "integrity of game files, then try again", 2)
        if hashlib.sha256(image).hexdigest() != pins["image_sha256"]:
            print("Your copy of the game has something added to it, most likely a mod. That's fine: its code is the "
                  "supported build, and that is all the build reads, so the install goes on. The log mentions it "
                  "once more as a warning; you can ignore that.")
        out = os.path.abspath(a.output)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out + ".tmp", "wb") as f:
            f.write(image)
        os.chmod(out + ".tmp", 0o755)
        os.replace(out + ".tmp", out)
    except Failure as e:
        print("error: %s." % e)
        return e.code
    print("Decrypted: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
