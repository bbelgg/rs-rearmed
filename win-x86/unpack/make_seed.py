#!/usr/bin/env python3
"""Build-time: extract the shippable seed (seed.bin) from a post-checksum capture.

Run ONCE by the developer; the output seed.bin ships with the installer. Contains only
VMProtect VM scratch + register state (no game code, no third-party code). The exe
content is NOT included — it comes from the user's own file at unpack time.

Two capture sources:

  From a Qiling snapshot (the old route, 0d42e2 build only):
    python3 -I make_seed.py snap/post_checksum_c.qlsnap <exe> seed.bin

  From a QEMU capture (qemu_capture.py, any build):
    python3 -I make_seed.py --from-gdb capture.bin <exe> seed.bin
"""
import sys, os, gzip, hashlib, pickle, argparse, re, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
PAGE = 0x1000


def make_seed_from_qlsnap(snap_path, exe_path):
    """Original Qiling snapshot route."""
    from lp_unpack import layout_exe_at_vas, pe_layout, file_sha256
    lay = pe_layout(exe_path)
    IMAGE_BASE, IMAGE_SIZE = lay.base, lay.size
    snap = pickle.load(open(snap_path, "rb"))

    snap_exe = bytearray(IMAGE_SIZE)
    def snap_page(addr):
        out = bytearray(PAGE); got = False
        p = addr & ~0xfff
        for a, sz, perms, label, data in snap['mem']['ram']:
            lo, hi = a, a + len(data)
            if hi <= p or lo >= p + PAGE:
                continue
            for i in range(PAGE):
                va = p + i
                if lo <= va < hi:
                    out[i] = data[va - lo]; got = True
        return bytes(out) if got else None
    for a, sz, perms, label, data in snap['mem']['ram']:
        if IMAGE_BASE <= a < IMAGE_BASE + IMAGE_SIZE:
            snap_exe[a - IMAGE_BASE:a - IMAGE_BASE + len(data)] = data

    disk_exe = layout_exe_at_vas(exe_path)
    exe_delta = [(i, snap_exe[i]) for i in range(IMAGE_SIZE) if snap_exe[i] != disk_exe[i]]

    STK_LO, STK_HI = 0xfffdd000, 0xffffe000
    heap_seed = [0x05004000, 0x05b15000]
    kern_targets = [0x10016b30, 0x1001c7f0, 0x1001ce30, 0x1001e770, 0x1002b030, 0x10031c20]

    pages = []
    for p in range(STK_LO, STK_HI, PAGE):
        d = snap_page(p)
        if d and d != b'\x00' * PAGE:
            pages.append((p, d))
    for p in heap_seed:
        d = snap_page(p)
        if d:
            pages.append((p & ~0xfff, d))

    gdt_addr = snap['mem']['ram'][1][0]
    gdt = snap['mem']['ram'][1][4]
    regs = {k: v for k, v in snap['reg'].items()}

    return {
        'exe_delta': exe_delta,
        'gdt_addr': gdt_addr,
        'gdt': gdt,
        'pages': pages,
        'kern_targets': kern_targets,
        'regs': regs,
        'exe_sha256': file_sha256(exe_path),
    }


def _module_ranges(pages):
    """The address ranges of the DLLs in a capture, from the PE headers among its pages."""
    ranges = []
    for addr, data in pages:
        if addr & 0xffff or data[:2] != b"MZ":
            continue
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if pe < PAGE - 0x100 and data[pe:pe + 4] == b"PE\0\0":
            ranges.append((addr, addr + struct.unpack_from("<I", data, pe + 24 + 56)[0]))
    return ranges


def _blank_text(data):
    """The process' environment and paths name the guest's user and machine, and nothing
    in the decompression reads them: make_seed checks that the image stays the same."""
    return re.sub(rb"(?:[\x20-\x7e]\x00){6,}", lambda m: bytes(len(m.group())), data)


def _ring0_segments(gdt, regs):
    """Unicorn starts in ring 0 and refuses a ring 3 stack segment, and a real guest's
    selectors are ring 3. Move those the capture uses to ring 0: same bases and limits."""
    gdt, regs = bytearray(gdt), dict(regs)
    for name in ("cs", "ss", "ds", "es", "fs", "gs"):
        regs[name] &= ~3
        if regs[name]:
            gdt[(regs[name] & ~7) + 5] &= ~0x60
    return bytes(gdt), regs


def relocation_sites(image, loaded, delta, base, rvas):
    """The offsets of the relocated words of the unpacked image, as gaps from one to the next,
    against a copy of the game loaded elsewhere. A word that differs by exactly the delta is one.
    So is a .data word pointing into the game that the running game changed, which the copy can
    no longer show; one the copy has unchanged was not relocated: it is data that looks like a
    pointer (the song signature key has such words)."""
    sites, i, end = set(), PAGE, min(len(image), len(loaded)) - 3
    while i < end:
        if image[i] == loaded[i]:
            i += 1
            continue
        for at in range(max(i - 3, 0), i + 1):      # the low bytes of a word can be the same
            have, got = struct.unpack_from("<II", image[at:at + 4] + loaded[at:at + 4])
            if (got - have) & 0xffffffff == delta:
                sites.add(at)
                i = at + 4
                break
        else:
            i += 1

    targets = [(base + rvas[n][0], base + sum(rvas[n])) for n in (".text", ".rdata", ".data")]
    lo, size = rvas[".data"]
    for at in range(lo, min(lo + size, end), 4):
        word = struct.unpack_from("<I", image, at)[0]
        if at not in sites and any(a <= word < b for a, b in targets) \
                and word != struct.unpack_from("<I", loaded, at)[0]:
            sites.add(at)
    ordered = sorted(sites)
    return [b - a for a, b in zip([0] + ordered, ordered)]


def make_seed_from_gdb(capture_path, exe_path, loaded_path=None, run_base=None):
    """QEMU capture route (qemu_capture.py output). loaded_path is a flat copy of the game
    loaded at run_base (a harvest's rs_harvest2.bin), for the relocation sites."""
    from lp_unpack import layout_exe_at_vas, pe_layout, file_sha256, run_seed
    with gzip.open(capture_path, "rb") as f:
        cap = pickle.load(f)

    if cap.get("format") != "qemu_gdb_capture_v2":
        raise ValueError("not a qemu_capture.py output (format: %s)" % cap.get("format"))
    lay = pe_layout(exe_path)
    if cap["exe_sha256"] != file_sha256(exe_path):
        raise ValueError("this capture was made from a different exe")
    # a page that was not resident in the guest still held what the file holds
    disk_exe = layout_exe_at_vas(exe_path)
    exe_delta = []
    for off, data in sorted(cap["image_pages"].items()):
        exe_delta += [(off + i, b) for i, b in enumerate(data)
                      if off + i < len(disk_exe) and b != disk_exe[off + i]]

    # DLL pages stay out of the seed: it carries no third-party code
    modules = _module_ranges(cap["pages"])
    scratch = {a: bytes(d) for a, d in cap["pages"]
               if d != b"\x00" * len(d) and not any(lo <= a < hi for lo, hi in modules)}

    gdt, regs = _ring0_segments(cap["gdt"], cap["regs"])
    seed = {
        'image_base': cap["image_base"],
        'exe_delta': exe_delta,
        'gdt_addr': cap["gdt_addr"],
        'gdt': gdt,
        'pages': [],
        'kern_targets': _find_kern_targets(cap, disk_exe, lay),
        'regs': regs,
        'exe_sha256': file_sha256(exe_path),
    }

    # of those, carry only the pages the decompression touches
    used = set()
    def fill(page):
        if page in scratch:
            used.add(page)
        return scratch.get(page)
    image, _ = run_seed(exe_path, seed, fill)
    seed['pages'] = [(a, _blank_text(scratch[a])) for a in sorted(used)]
    seed['output_sha256'] = hashlib.sha256(image).hexdigest()

    again, _ = run_seed(exe_path, seed)
    if again != image:
        raise ValueError("the decompression is not repeatable from the trimmed seed")

    if loaded_path:
        with open(loaded_path, "rb") as f:
            loaded = f.read()
        seed["link_base"], seed["run_base"] = lay.base, run_base
        seed["relocs"] = relocation_sites(image, loaded, run_base - lay.base, lay.base, lay.rvas)
    return seed


def _find_kern_targets(cap, disk_exe, lay):
    """The kernel32/ntdll addresses in the exe's import table, as the guest's loader resolved
    them. VMProtect reads the first byte of each target to check for hooks (0xcc, 0xe9, ...);
    lp_unpack places a benign prologue (8b ff 55 8b ec) at each one."""
    image = bytearray(disk_exe)
    for off, data in cap["image_pages"].items():
        image[off:off + len(data)] = data
    pe = struct.unpack_from("<I", image, 0x3c)[0]
    import_rva, import_size = struct.unpack_from("<II", image, pe + 24 + 104)
    targets = []
    for off in range(import_rva, import_rva + import_size, 20):
        ilt_rva, iat_rva = (struct.unpack_from("<I", image, off + o)[0] for o in (0, 16))
        if not ilt_rva and not iat_rva:
            break
        while iat_rva + 4 <= len(image):
            entry = struct.unpack_from("<I", image, iat_rva)[0]
            if not entry:
                break
            if not cap["image_base"] <= entry < cap["image_base"] + lay.size:
                targets.append(entry)
            iat_rva += 4
    return sorted(set(targets))


def main():
    ap = argparse.ArgumentParser(
        description="Extract the shippable seed from a post-checksum capture")
    ap.add_argument("capture", help="Qiling .qlsnap, or --from-gdb capture.bin")
    ap.add_argument("exe", help="the depot Rocksmith2014.exe this capture was made from")
    ap.add_argument("output", help="output seed.bin path")
    ap.add_argument("--from-gdb", action="store_true",
                    help="capture is from qemu_capture.py (not a Qiling snapshot)")
    ap.add_argument("--loaded", help="a flat copy of the game loaded at --run-base, for the "
                    "relocation sites (with --from-gdb)")
    ap.add_argument("--run-base", type=lambda v: int(v, 0), default=0xf80000,
                    help="the base the port's tables are for")
    args = ap.parse_args()

    if args.from_gdb:
        seed = make_seed_from_gdb(args.capture, args.exe, args.loaded, args.run_base)
    else:
        seed = make_seed_from_qlsnap(args.capture, args.exe)

    with gzip.open(args.output, "wb") as f:
        f.write(b"LPSEED01")
        pickle.dump(seed, f, protocol=4)

    print(f"exe_delta: {len(seed['exe_delta'])} bytes")
    print(f"carried pages: {len(seed['pages'])} "
          f"({sum(len(d) for _, d in seed['pages']) // 1024} KB uncompressed)")
    print(f"kern_targets: {len(seed['kern_targets'])}")
    print(f"relocation sites: {len(seed.get('relocs', []))}")
    sz = os.path.getsize(args.output)
    print(f"seed.bin: {sz} bytes ({sz / 1024:.1f} KB, gzip)")


if __name__ == "__main__":
    main()
