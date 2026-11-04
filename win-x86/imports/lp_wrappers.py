#!/usr/bin/env python3
"""wrappers_raw.tsv (lp_wrappers_run.py) -> wrappers.tsv for the front end's --wrappers option.

Columns: call_site, stub, kind, resume, pushed_reg, import, pushes, sets   (link addresses)

Stack rules, from the run that started at `patch` with sentinel registers:
  esp -4, [esp] in image          call; resume = [esp]. If patch < the call (a push byte first)
                                  and the stub consumed it, pushed_reg = that register.
  esp -8, [esp] in image, [esp+4] a sentinel
                                  the push before the call is a real argument push: the site is
                                  the call itself (patch + 1), no pushed_reg.
  esp  0                          jmp (tail jump through the import).
  esp -4n, [esp] in image, the n-1 words above it each a constant, a register or a register
  plus an offset, the word at the entry esp untouched
                                  a stub that pushes the call's arguments itself: the site is
                                  `patch` (what runs before it is ordinary code), the words go
                                  in the pushes column. Telling a constant from a register takes
                                  a second run with other sentinels (wrappers_raw_alt.tsv,
                                  lp_wrappers_run.py --alt); without it these stay left out.
                                  Other registers a stub changes are dead after the call (the
                                  program runs the same on Windows), except one left holding the
                                  import's address (the `mov reg, [imp]; call reg` the stub
                                  replaced: the code calls reg again later): the sets column.
  back in .text with esp unchanged, a register holding an export
                                  a stub standing for `mov reg, [imp]`: kind load, the register
                                  in pushed_reg, resume where the code came back, the import the
                                  export the register holds (not the one the code calls next).
  the same after a `pop r` byte before the call, with esp -4 and r's old value on top
                                  the stub pushes back what the pop took (the 6-byte `mov reg,
                                  [imp]` became `pop r; call stub`): kind load, the site the pop.
  the same from a `push r` at patch, with esp -4 and r's old value on top
                                  the push is the program's own (it saves r before loading it):
                                  kind load, the site the call after it.
Anything else is left out (decoded as today) and listed in wrappers_left.tsv.

Names: a target address can carry several names (an export, and other DLLs' exports that forward
to it). Prefer a name in one of the DLLs the game imports statically, then the direct export.
Usage: lp_wrappers.py HARVEST_DIR wrappers_raw.tsv OUTDIR [wrappers_raw_alt.tsv]
"""
import re
import struct
import sys

d, raw, outdir = sys.argv[1:4]
alt = sys.argv[4] if len(sys.argv) > 4 else None
GAME_DLLS = {"advapi32", "dinput8", "iphlpapi", "kernel32", "msvcp100", "msvcr100", "oleaut32", "rpcrt4",
             "setupapi", "shell32", "user32", "wininet", "winmm", "ws2_32", "bink2w32", "d3dx9_42", "ole32",
             "steam_api"}
SENT = {"a0a0a0a0": "eax", "b0b0b0b0": "ebx", "c0c0c0c0": "ecx", "d0d0d0d0": "edx", "51515151": "esi",
        "d1d1d1d1": "edi", "bebebebe": "ebp"}
PUSH = {0x50: "eax", 0x51: "ecx", 0x52: "edx", 0x53: "ebx", 0x55: "ebp", 0x56: "esi", 0x57: "edi"}
POP = {k + 8: r for k, r in PUSH.items()}

info = dict(l.rstrip("\r\n").split("\t", 1) for l in open(d + "/rs_harvest2.txt", encoding="utf-8-sig") if "\t" in l)
delta = int(info["game_base"], 16) - 0x400000
img = open(d + "/rs_harvest2_last.bin", "rb").read()


def bn(m):
    m = m.lower()
    return m[:-4] if m.endswith(".dll") else m


at, fwd_to, where = {}, {}, {}
for l in list(open(d + "/rs_exports.tsv", encoding="utf-8-sig"))[1:]:
    r = l.rstrip("\r\n").split("\t")
    if len(r) < 4 or not r[2]:
        continue
    m = bn(r[0])
    if len(r) > 4 and r[4]:
        tm, tn = r[4].split(".", 1)
        fwd_to.setdefault((bn(tm), tn), []).append((m, r[2]))
    else:
        at.setdefault(int(r[3], 16), []).append((m, r[2]))
        where[(m, r[2])] = int(r[3], 16)


def aliases(m, n, depth=0):
    """Exports that forward to m!n, directly or through api-ms-win / kernelbase hops."""
    out = list(fwd_to.get((m, n), []))
    if depth < 3:
        for (am, an) in list(out):
            if am.startswith("api-ms-win-") or am.startswith("ext-ms-") or am == "kernelbase":
                out += aliases(am, an, depth + 1)
    if depth == 0:
        # kernel32 -> api-ms-win-* -> kernelbase: forwarders name the api set, not kernelbase
        for (fm, fn), lst in fwd_to.items():
            if fn == n and (fm.startswith("api-ms-win-") or fm.startswith("ext-ms-")):
                out += lst
    return out


def name_of(imp):
    m = re.match(r"([^!]+)!(.+)", imp)
    if not m:
        return ""
    mod, nm = bn(m.group(1)), m.group(2)
    va = where.get((mod, nm))
    cands = at.get(va, [(mod, nm)]) if va is not None else [(mod, nm)]
    allc = []
    for c in cands:
        allc.append(c)
        allc += aliases(*c)
    for c in allc:
        if c[0] in GAME_DLLS:
            return "%s.dll!%s" % c
    return "%s.dll!%s" % cands[0]


REG_SENT = {n: int(v, 16) for v, n in SENT.items()}
ALT = {"eax": 0x11223344, "ebx": 0x22446688, "ecx": 0x33557799, "edx": 0x4466aacc, "esi": 0x5577bbdd,
       "edi": 0x6688ccee, "ebp": 0x7799ddff}  # lp_wrappers_run.py --alt


def stack_of(note):
    m = re.search(r"esp delta (-?\d+) stack ([0-9a-f,]+)", note)
    return (int(m.group(1)), [int(w, 16) for w in m.group(2).split(",")]) if m else None


alt_stack = {}
if alt:
    for l in list(open(alt))[1:]:
        f = (l.rstrip("\n").split("\t") + [""] * 7)[:7]
        alt_stack[f[0]] = stack_of(f[6])


def pushes_of(patch, note):
    """The words a stub pushed (in push order, as lp_wrappers' pushes column) and the resume
    address, or None when the run's stack is not that shape."""
    a, b = stack_of(note), alt_stack.get(patch)
    if not a or not b or a[0] != b[0] or a[0] >= 0 or a[0] % 4:
        return None
    n = -a[0] // 4
    wa, wb = a[1], b[1]
    if len(wa) <= n or len(wb) <= n or wa[0] != wb[0] or wa[n] != 0 or wb[n] != 0:
        return None
    if not 0 <= wa[0] - delta - 0x400000 < len(img):
        return None
    out = []
    for x, y in zip(wa[1:n], wb[1:n]):
        if x == y:
            out.append("0x%x" % (x - delta if 0 <= x - delta - 0x400000 < len(img) else x))
            continue
        for r, v in REG_SENT.items():
            k = (x - v) & 0xffffffff
            if k == (y - ALT[r]) & 0xffffffff and (k < 0x10000 or k > 0xffff0000):
                out.append(r if not k else "%s+0x%x" % (r, k) if k < 0x10000 else "%s-0x%x" % (r, 0x100000000 - k))
                break
        else:
            return None
    return out[::-1], wa[0] - delta


def sets_of(imp, note):
    """Registers the run left holding the import's own address (sets column)."""
    m = re.match(r"([^!]+)!(.+)", imp)
    va = where.get((bn(m.group(1)), m.group(2))) if m else None
    ch = re.search(r"regs changed: (\S+)", note)
    if va is None or not ch:
        return ""
    return ",".join(r for r, v in (x.split("=") for x in ch.group(1).split(",") if "=" in x) if int(v, 16) == va)


def load_of(note, pop=None):
    """(resume, register, import) of a stub that only loads a register with an import, or None.
    pop: the register a `pop r` byte just before the stub's call takes off the stack; the stub
    then pushes that register's old value back (esp -4, the word on top its sentinel), so with
    the pop the stack is as it was."""
    m = re.search(r"back (0x[0-9a-f]+) delta (-?\d+) regs (\S+)(?: top ([0-9a-f]+))?", note)
    if not m or m.group(3) == "-":
        return None
    if int(m.group(2)) != 0 and not (pop and int(m.group(2)) == -4 and SENT.get(m.group(4) or "") == pop):
        return None
    hits = []
    for kv in m.group(3).split(","):
        r, v = kv.split("=")
        names = at.get(int(v, 16))
        if names:
            hits.append((r, names))
    if len(hits) != 1:
        return None
    r, names = hits[0]
    pick = next((c for c in names if c[0] in GAME_DLLS), names[0])
    return int(m.group(1), 16), r, name_of("%s.dll!%s" % pick)


rows, left, sets = [], [], {}
for l in list(open(raw))[1:]:
    patch, stub, kind, resume, form, imp, note = (l.rstrip("\n").split("\t") + [""] * 7)[:7]
    sets[stub] = sets_of(imp, note) if "!" in imp else ""
    p = int(patch, 16)
    imp2 = name_of(imp) if "!" in imp else ""
    m = re.search(r"esp delta (-?\d+) stack ([0-9a-f,]+)", note)
    o = p - 0x400000
    b0, b1 = img[o], img[o + 1]
    ld = load_of(note)   # before the call / jmp rules: such a run ends at whatever the code calls next
    pl = None if ld or b0 != 0xe8 else load_of(note, POP.get(img[o - 1]))
    # a real `push r` at patch, run with the stub: esp -4 and r's old value on top are its own
    pu = None if ld or pl or b0 not in PUSH or b1 != 0xe8 else load_of(note, PUSH[b0])
    if ld and 5 <= ld[0] - p <= 16:
        rows.append((p, stub, "load", "0x%08x" % ld[0], ld[1], ld[2]))
        sets[stub] = ""
    elif pl and 5 <= pl[0] - (p - 1) <= 16:
        rows.append((p - 1, stub, "load", "0x%08x" % pl[0], pl[1], pl[2]))
        sets[stub] = ""
    elif pu and 5 <= pu[0] - (p + 1) <= 16:
        rows.append((p + 1, stub, "load", "0x%08x" % pu[0], pu[1], pu[2]))
        sets[stub] = ""
    elif kind == "call":
        site, preg = (p, PUSH[b0]) if b0 in PUSH and b1 == 0xe8 else (p, "")   # push r; call stub: the stub consumed the push
        if not 5 <= int(resume, 16) - site <= 16:
            left.append(l.rstrip("\n") + " resume too far")
            continue
        rows.append((site, stub, "call", resume, preg, imp2))
    elif kind == "jmp":
        rows.append((p, stub, "jmp", "", PUSH[b0] if b0 in PUSH and b1 == 0xe8 else "", imp2))
    elif m and int(m.group(1)) == -8 and m.group(2).split(",")[1] in SENT and b0 in PUSH and b1 == 0xe8:
        ret = int(m.group(2).split(",")[0], 16) - delta
        rows.append((p + 1, stub, "call", "0x%08x" % ret, "", imp2))
    elif imp2 and pushes_of(patch, note) and 5 <= pushes_of(patch, note)[1] - p <= 16:
        pw, ret = pushes_of(patch, note)
        rows.append((p, stub, "call", "0x%08x" % ret, "", imp2, ",".join(pw)))
    else:
        left.append(l.rstrip("\n"))

keep = []
for r in rows:
    if r[2] == "call" and not 5 <= int(r[3], 16) - r[0] <= 16:
        left.append("0x%08x\t%s\tcall\t%s\t\t%s\tresume too far" % (r[0], r[1], r[3], r[5]))
    else:
        keep.append(r)
rows = keep
with open(outdir + "/wrappers.tsv", "w") as f:
    f.write("# Generated from a harvester v2 capture by black-box runs of each VMProtect import wrapper.\n")
    f.write("call_site\tstub\tkind\tresume\tpushed_reg\timport\tpushes\tsets\n")
    for r in rows:
        site, stub, kind, resume, preg, imp = r[:6]
        pw, st = r[6] if len(r) > 6 else "", sets.get(r[1], "")
        f.write("0x%08x\t%s\t%s\t%s\t%s\t%s%s\n" % (site, stub, kind, resume, preg, imp,
                                                    "\t%s\t%s" % (pw, st) if st else "\t" + pw if pw else ""))
with open(outdir + "/wrappers_left.tsv", "w") as f:
    f.write("\n".join(left) + "\n")
named = sum(1 for r in rows if r[5])
print("%d rows (%d named), %d left out" % (len(rows), named, len(left)))
