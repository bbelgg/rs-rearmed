#!/usr/bin/env python3
"""Offline VMProtect unpacker for the Learn & Play Windows exe (shippable form).

Input:  the user's own pinned Rocksmith2014.exe (Steam depot 221683) + a shipped seed
        (seed.bin, ~KB of VMProtect VM scratch — no game code, no third-party code).
Output: the decompressed image (all sections at their link-base RVAs, before relocation),
        written to a file.

No Windows, no snapshot, no real DLLs. The seed lets us skip VMProtect's un-emulatable
anti-debug bootstrap and resume at the point where decompression begins; the decompression
itself is deterministic (identical sha256 across runs). See README.md beside this file.

Dependencies: unicorn, pefile (both ship prebuilt arm64 wheels). Everything else is stdlib.
"""
import sys, os, gzip, pickle, struct, hashlib, argparse
import pefile
from unicorn import *
from unicorn.x86_const import *

PAGE = 0x1000
PROLOGUE = bytes.fromhex("8bff558bec5dc3")  # benign MSVC hot-patch prologue (anti-hook byte0)

REG_MAP = {
    'eax':UC_X86_REG_EAX,'ebx':UC_X86_REG_EBX,'ecx':UC_X86_REG_ECX,'edx':UC_X86_REG_EDX,
    'esi':UC_X86_REG_ESI,'edi':UC_X86_REG_EDI,'ebp':UC_X86_REG_EBP,'esp':UC_X86_REG_ESP,
    'eip':UC_X86_REG_EIP,'eflags':UC_X86_REG_EFLAGS,'cs':UC_X86_REG_CS,'ss':UC_X86_REG_SS,
    'ds':UC_X86_REG_DS,'es':UC_X86_REG_ES,'fs':UC_X86_REG_FS,'gs':UC_X86_REG_GS,
}


class Layout:
    """The exe's own layout, from its PE header: base, size and the two sections VMProtect
    unprotects before it decompresses into them."""

    def __init__(self, pe):
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        secs = {s.Name.rstrip(b"\0").decode("latin-1"): s for s in pe.sections}
        self.rvas = {n: (s.VirtualAddress, s.Misc_VirtualSize) for n, s in secs.items()}
        for name in (".text", ".rdata"):
            if name not in secs:
                raise ValueError("the exe has no %s section: not the Learn & Play exe" % name)
        self.text_lo = self.base + secs[".text"].VirtualAddress
        self.text_vsize = secs[".text"].Misc_VirtualSize
        self.rdata = self.base + secs[".rdata"].VirtualAddress


    def load_at(self, base):
        """The bootstrap's pointers are absolute: run the image where the capture had it."""
        delta = base - self.base
        self.base, self.text_lo, self.rdata = base, self.text_lo + delta, self.rdata + delta


def pe_layout(exe_path):
    return Layout(pefile.PE(exe_path, fast_load=True))


def layout_exe_at_vas(exe_path):
    """Lay the on-disk PE out at its section VAs — the image, the user's own material."""
    pe = pefile.PE(exe_path, fast_load=True)
    size = pe.OPTIONAL_HEADER.SizeOfImage
    img = bytearray(size)
    img[0:len(pe.header)] = pe.header
    for s in pe.sections:
        va, raw = s.VirtualAddress, s.get_data()
        end = min(va + len(raw), size)
        img[va:end] = raw[:end - va]
    return img


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class _SeedUnpickler(pickle.Unpickler):
    """The seed is plain data (dicts, lists, tuples, ints, bytes, str): refuse anything that
    would construct an object, so a tampered seed cannot run code."""

    def find_class(self, module, name):
        raise pickle.UnpicklingError("seed.bin holds a non-data object (%s.%s)" % (module, name))


def load_seed(seed_path):
    with gzip.open(seed_path, "rb") as f:
        magic = f.read(8)
        if magic != b"LPSEED01":
            raise ValueError("bad seed magic")
        return _SeedUnpickler(f).load()


def check_seed(seed, exe_path):
    """A seed is keyed to one exe build. Seeds made before the key was recorded carry none."""
    want = seed.get("exe_sha256")
    if want is None:
        sys.stderr.write("lp_unpack: this seed does not record its exe build; "
                         "the output is only right for the build it was made from\n")
        return
    got = file_sha256(exe_path)
    if got != want:
        raise ValueError("seed.bin was made for a different Rocksmith2014.exe (sha256 %s), "
                         "this one is %s: it needs a seed for this build" % (want, got))


def unpack(exe_path, seed_path):
    seed = load_seed(seed_path)
    check_seed(seed, exe_path)
    img, error = run_seed(exe_path, seed)
    want = seed.get("output_sha256")
    if want is None:
        if error:
            raise error
    elif hashlib.sha256(img).hexdigest() != want:
        raise ValueError("the unpacked image is not the one this seed makes" +
                         (": %s" % error if error else ""))
    return relocate(img, seed)


def relocate(img, seed):
    """The decompressed image is linked for the exe's ImageBase. VMProtect's relocation table
    stays packed, so the seed carries the sites, as gaps between them, and the base the rest of
    the port's tables are for. A seed without them gives the image as it is."""
    if not seed.get("relocs"):
        return img
    out = bytearray(img)
    pe = struct.unpack_from("<I", out, 0x3c)[0]
    delta = seed["run_base"] - seed["link_base"]
    site = 0
    for gap in seed["relocs"]:
        site += gap
        word = struct.unpack_from("<I", out, site)[0]
        struct.pack_into("<I", out, site, (word + delta) & 0xffffffff)
    struct.pack_into("<I", out, pe + 24 + 28, seed["run_base"])
    return bytes(out)


def write_image(exe_path, seed_path, out_path):
    """Unpack to out_path, with the relocation sites beside it in out_path.relocs (32-bit
    offsets): the recompiler takes functions reached only through a pointer from them."""
    img = unpack(exe_path, seed_path)
    with open(out_path, "wb") as f:
        f.write(img)
    site, sites = 0, []
    for gap in load_seed(seed_path).get("relocs", []):
        site += gap
        sites.append(site)
    if sites:
        with open(out_path + ".relocs", "wb") as f:
            f.write(struct.pack("<%dI" % len(sites), *sites))
    return img


def run_seed(exe_path, seed, fill=None):
    """Run the decompression and return (image, the error that ended it, if one did). The
    bootstrap has nothing to run once the sections are done, so with a real guest's state
    it ends in a fault: the seed's output hash says whether the image is whole. fill(page)
    supplies the bytes of a page the run touches that is not mapped yet."""
    lay = pe_layout(exe_path)
    lay.load_at(seed.get('image_base', lay.base))

    # exe image = on-disk layout + the tiny .UBX1 scratch delta from the seed
    img = layout_exe_at_vas(exe_path)
    for off, b in seed['exe_delta']:
        img[off] = b

    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(lay.base, lay.size, UC_PROT_ALL)
    uc.mem_write(lay.base, bytes(img))

    # GDT (FS->TEB, flat SS descriptor)
    gdt_addr, gdt = seed['gdt_addr'], seed['gdt']
    uc.mem_map(gdt_addr & ~0xfff, ((len(gdt) + 0xfff) & ~0xfff) or PAGE, UC_PROT_ALL)
    uc.mem_write(gdt_addr, gdt)
    uc.reg_write(UC_X86_REG_GDTR, (0, gdt_addr, len(gdt) - 1, 0))

    # carried VM scratch: stack + heap seed pages
    for base, data in seed['pages']:
        uc.mem_map(base, ((len(data) + 0xfff) & ~0xfff) or PAGE, UC_PROT_ALL)
        uc.mem_write(base, data)

    # synthetic kernel32 stub pages (benign prologue at each call target; no real bytes)
    kern_targets = seed['kern_targets']
    for p in sorted({a & ~0xfff for a in kern_targets}):
        uc.mem_map(p, PAGE, UC_PROT_ALL)
        uc.mem_write(p, b'\x90' * PAGE)
    for a in kern_targets:
        uc.mem_write(a, PROLOGUE)

    for name, val in seed['regs'].items():
        if name in REG_MAP:
            uc.reg_write(REG_MAP[name], val)
    eip = seed['regs']['eip']

    stopped = [False]
    def hook_stub(uc, address, size, ud):
        esp = uc.reg_read(UC_X86_REG_ESP)
        a0 = struct.unpack('<I', bytes(uc.mem_read(esp + 4, 4)))[0]
        a1 = struct.unpack('<I', bytes(uc.mem_read(esp + 8, 4)))[0]
        a3 = struct.unpack('<I', bytes(uc.mem_read(esp + 16, 4)))[0]
        ret = struct.unpack('<I', bytes(uc.mem_read(esp, 4)))[0]
        if (a0 == lay.text_lo and a1 == lay.text_vsize) or a0 == lay.rdata:
            try: uc.mem_write(a3, struct.pack('<I', 0x02))
            except UcError: pass
        if a0 == 0xdeadc0de:  # VMProtect's post-decompress ExitProcess
            stopped[0] = True; uc.emu_stop()
            uc.reg_write(UC_X86_REG_ESP, esp + 4); uc.reg_write(UC_X86_REG_EIP, 0xFFFFFFFF)
            return
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EAX, 1)
        uc.reg_write(UC_X86_REG_EIP, ret)
    for a in kern_targets:
        uc.hook_add(UC_HOOK_CODE, hook_stub, begin=a, end=a)

    def hook_unmapped(uc, access, address, size, value, ud):
        page = address & ~0xfff
        try: uc.mem_map(page, PAGE, UC_PROT_ALL)
        except UcError: return False
        data = fill(page) if fill else None
        if data: uc.mem_write(page, data)
        return True
    uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, hook_unmapped)

    def hook_int(uc, intno, ud):
        if intno == 3:
            uc.reg_write(UC_X86_REG_EIP, uc.reg_read(UC_X86_REG_EIP) + 1)
    uc.hook_add(UC_HOOK_INTR, hook_int)

    error = None
    try:
        uc.emu_start(eip, 0xFFFFFFFF, timeout=600_000_000)
    except UcError as e:
        if not stopped[0]:
            error = e
    return bytes(uc.mem_read(lay.base, lay.size)), error


def main():
    ap = argparse.ArgumentParser(description="Offline VMProtect unpack (Learn & Play Windows exe)")
    ap.add_argument("exe", help="path to the user's Rocksmith2014.exe (depot 221683)")
    ap.add_argument("seed", help="path to seed.bin")
    ap.add_argument("out", help="output path for the decompressed image")
    ap.add_argument("--expect-sha256", help="optional: verify output sha256")
    args = ap.parse_args()

    img = write_image(args.exe, args.seed, args.out)
    sha = hashlib.sha256(img).hexdigest()
    print(f"wrote {args.out} ({len(img)} bytes) sha256={sha}")
    if args.expect_sha256:
        ok = sha == args.expect_sha256
        print("sha256 verify:", "OK" if ok else "MISMATCH")
        sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()
