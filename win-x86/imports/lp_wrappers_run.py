#!/usr/bin/env python3
"""Run each VMProtect import-wrapper site as a black box on a harvester v2 capture.

Starts at the patched site with sentinel registers, runs until execution reaches an address in a
loaded module, and reports which export that is, plus the stack effect and return address.
Usage: lp_wrappers_run.py HARVEST_DIR IAT_MAP OUT_TSV
"""
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_UNMAPPED, UC_HOOK_BLOCK, UC_MEM_FETCH_UNMAPPED, UcError
from unicorn.x86_const import *

LINK = 0x400000
PAGE = 0x1000
STACK = 0x0f000000
STACK_SZ = 0x100000
SENT = {UC_X86_REG_EAX: 0xa0a0a0a0, UC_X86_REG_EBX: 0xb0b0b0b0, UC_X86_REG_ECX: 0xc0c0c0c0,
        UC_X86_REG_EDX: 0xd0d0d0d0, UC_X86_REG_ESI: 0x51515151, UC_X86_REG_EDI: 0xd1d1d1d1,
        UC_X86_REG_EBP: 0xbebebebe}
REGNAME = {v: k for k, v in {"eax": 0xa0a0a0a0, "ebx": 0xb0b0b0b0, "ecx": 0xc0c0c0c0, "edx": 0xd0d0d0d0,
                             "esi": 0x51515151, "edi": 0xd1d1d1d1, "ebp": 0xbebebebe}.items()}
# A second run with other sentinels (--alt; their differences differ too) tells the words a stub
# pushes apart: a constant is the same in both runs, a register (or register + offset) moves
# with its sentinel, anything computed from two registers fits neither (lp_wrappers.py takes
# both outputs).
ALT = {"eax": 0x11223344, "ebx": 0x22446688, "ecx": 0x33557799, "edx": 0x4466aacc, "esi": 0x5577bbdd,
       "edi": 0x6688ccee, "ebp": 0x7799ddff}
FILL = b"\0"  # what the pages a stub touches outside the image read as (--alt: 0xa5, so a word
# read from them is not taken for a constant)
if "--alt" in sys.argv:
    sys.argv.remove("--alt")
    FILL = b"\xa5"
    SENT = {r: ALT[REGNAME[v]] for r, v in SENT.items()}
    REGNAME = {ALT[n]: n for n in REGNAME.values()}

d, iat_map, out = sys.argv[1:4]
info = dict(l.rstrip("\r\n").split("\t", 1) for l in open(d + "/rs_harvest2.txt", encoding="utf-8-sig") if "\t" in l)
base, size = int(info["game_base"], 16), int(info["game_size"], 16)
delta = base - LINK
img = open(d + "/rs_harvest2_last.bin", "rb").read()

# .text from the harvested header: where the code goes on after a stub that only loads a register
_o = struct.unpack_from("<I", img, 0x3c)[0]
_e = _o + 24 + struct.unpack_from("<H", img, _o + 20)[0]
def section(name):
    return next((base + struct.unpack_from("<I", img, _e + 40 * k + 12)[0],
                 base + struct.unpack_from("<I", img, _e + 40 * k + 12)[0] + struct.unpack_from("<I", img, _e + 40 * k + 8)[0])
                for k in range(struct.unpack_from("<H", img, _o + 6)[0]) if img[_e + 40 * k:_e + 40 * k + 8].rstrip(b"\0") == name)


TEXT = section(b".text")
UBX0 = section(b".UBX0")   # where the stubs are

mods = []
for l in list(open(d + "/rs_modules.tsv", encoding="utf-8-sig"))[1:]:
    r = l.rstrip("\r\n").split("\t")
    mods.append((r[0], int(r[1], 16), int(r[2], 16)))
exports = {}
fwd_alias = {}
for l in list(open(d + "/rs_exports.tsv", encoding="utf-8-sig"))[1:]:
    r = l.rstrip("\r\n").split("\t")
    if len(r) < 4:
        continue
    if len(r) > 4 and r[4]:
        fwd_alias.setdefault(r[4].split(".", 1)[-1], []).append(r[0] + "!" + r[2])
        continue
    exports.setdefault(int(r[3], 16), []).append((r[0], r[2] or "#" + r[1]))


def module_of(va):
    for n, b, s in mods:
        if b <= va < b + s and n.lower() != "rocksmith2014.exe":
            return n, b
    return None


rows = []
for l in open(iat_map):
    r = l.rstrip("\n").split("\t")
    if r[0] != "wrap":
        continue
    det = dict(kv.split("=", 1) for kv in r[5].split() if "=" in kv)
    site, patch = int(r[1], 16), int(det.get("patch", r[1]), 16)
    # a stub that starts with a pop after no push byte (`test bl, bl; call stub`): the byte before
    # is the end of another instruction, the run starts at the call
    if patch == site - 1 and not 0x50 <= img[patch + delta - base] <= 0x57:
        patch = site
    rows.append((site, int(det["stub"], 16), det.get("form", ""), patch, det.get("op", "")))

uc = Uc(UC_ARCH_X86, UC_MODE_32)
uc.mem_map(base, size)
uc.mem_write(base, img)
uc.mem_map(STACK, STACK_SZ)
# flat FS so fs: reads don't fault (TEB-ish zero page)
mapped = set()


def on_unmapped(uc, access, addr, sz, val, ud):
    if access == UC_MEM_FETCH_UNMAPPED:
        hit["t"] = addr                     # execution left the image: the import target
        return False
    p = addr & ~0xfff
    for q in (p, (addr + sz - 1) & ~0xfff):
        if q not in mapped:
            try:
                uc.mem_map(q, PAGE)
                uc.mem_write(q, FILL * PAGE)
            except UcError:
                pass
            mapped.add(q)
    return True


hit = {}


def on_block(uc, addr, sz, ud):
    # the first block back in .text after the stub (not the site's own): where a stub that only
    # loads a register returns to, with the registers as it left them and the word at esp
    if "back" not in hit and TEXT[0] <= addr < TEXT[1] and addr != hit.get("start"):
        esp = uc.reg_read(UC_X86_REG_ESP)
        hit["back"] = (addr, esp, {REGNAME[v]: uc.reg_read(r) for r, v in SENT.items() if uc.reg_read(r) != v},
                       struct.unpack("<I", uc.mem_read(esp, 4))[0])
    if not (base <= addr < base + size):
        hit["t"] = addr
        uc.emu_stop()


uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)
uc.hook_add(UC_HOOK_BLOCK, on_block)

res = []
stats = {}
known = {r[0] for r in rows} | {r[3] for r in rows}


def chained(ra):
    """A wrapper site right where a stub came back to (`call stub` or `push r; call stub`): the
    sweep that found the sites (lp_iat.py) loses step on the junk byte after a stub's call and
    misses a site that follows at once. (site, stub, form, patch) at link addresses, or None."""
    o = ra - base
    if not 0 <= o < len(img) - 6:
        return None
    p = o + 1 if 0x50 <= img[o] <= 0x57 and img[o + 1] == 0xe8 else o
    if img[p] != 0xe8:
        return None
    d = (base + p + 5 + struct.unpack_from("<i", img, p + 1)[0]) & 0xffffffff
    if not UBX0[0] <= d < UBX0[1] or base + p - delta in known:
        return None
    return base + p - delta, d - delta, "chain_pop" if p != o else "chain_push", ra - delta


qi = 0
while qi < len(rows):
    site, stub, form, patch, op = rows[qi]
    qi += 1
    rsite, rpatch = site + delta, patch + delta
    hit.clear()
    hit["start"] = rpatch
    for reg, v in SENT.items():
        uc.reg_write(reg, v)
    esp0 = STACK + STACK_SZ // 2
    uc.mem_write(esp0 - 0x100, b"\0" * 0x200)
    uc.reg_write(UC_X86_REG_ESP, esp0)
    uc.reg_write(UC_X86_REG_EFLAGS, 0x202)
    err = ""
    try:
        uc.emu_start(rpatch, 0xffffffff, count=200000)
    except UcError as e:
        err = str(e)
    t = hit.get("t")
    if t is None:
        try:
            eip = uc.reg_read(UC_X86_REG_EIP)
            m = module_of(eip)
            if m:
                t = eip
        except UcError:
            pass
    imp, kind, resume, preg, why = "", "", "", "", ""
    if t is not None:
        names = exports.get(t)
        m = module_of(t)
        if names:
            imp = "%s!%s" % (names[0][0].lower(), names[0][1])
        elif m:
            imp = "%s+0x%x" % (m[0].lower(), t - m[1])
            why = "not an export"
        esp = uc.reg_read(UC_X86_REG_ESP)
        top = struct.unpack("<I", uc.mem_read(esp, 4))[0]
        if esp == esp0 - 4 and (base <= top < base + size):
            kind, resume = "call", "0x%08x" % (top - delta)
        elif esp == esp0:
            kind = "jmp"
        else:
            kind = "?"
            words = []
            for k in range(max(4, min(16, (esp0 - esp) // 4 + 1))):  # down to the entry esp
                try:
                    words.append("%08x" % struct.unpack("<I", uc.mem_read(esp + 4 * k, 4))[0])
                except UcError:
                    break
            why = (why + " " if why else "") + "esp delta %d stack %s" % (esp - esp0, ",".join(words))
        # a register clobbered vs sentinels means the site used it (push r ... pop/xchg)
        # (with the value: a stub may leave the import's address in one, as the
        # `mov reg, [imp]; call reg` it replaced did)
        changed = ["%s=%08x" % (REGNAME[v], uc.reg_read(reg)) for reg, v in SENT.items() if uc.reg_read(reg) != v]
        if changed:
            why = (why + " " if why else "") + "regs changed: " + ",".join(changed)
        if form.startswith("stub_push") or form.endswith("push"):
            pass
    else:
        why = err or "no module reached"
    if "back" in hit:  # where execution first came back to .text: address, esp delta, changed registers
        ba, besp, bregs, btop = hit["back"]
        why = (why + " " if why else "") + "back 0x%08x delta %d regs %s top %08x" % (
            ba - delta, besp - esp0, ",".join("%s=%08x" % kv for kv in sorted(bregs.items())) or "-", btop)
    res.append((patch, stub, kind, resume, form, imp, why))
    key = "named" if imp and "!" in imp else ("module" if imp else "none")
    stats[key] = stats.get(key, 0) + 1
    # after the call (the return address it left) or where a load came back
    ch = chained(int(resume, 16) + delta) if resume else chained(hit["back"][0]) if "back" in hit else None
    if ch:
        known.add(ch[0])
        rows.append((ch[0], ch[1], ch[2], ch[3], "call"))
        stats["chained"] = stats.get("chained", 0) + 1

with open(out, "w") as f:
    f.write("# patch\tstub\tkind\tresume\tform\timport\tnote\n")
    for p, s, k, rs, fm, imp, why in sorted(res):
        f.write("0x%08x\t0x%08x\t%s\t%s\t%s\t%s\t%s\n" % (p, s, k, rs, fm, imp, why))
print(len(res), "sites:", stats)
