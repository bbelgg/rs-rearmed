#!/usr/bin/env python3
"""Capture the VMProtect state at the start of decompression from a QEMU guest.

Developer only, run once per exe build. Talks to QEMU's GDB stub directly, so it needs no
gdb and nothing but the standard library.

The watchpoint has to be set before the exe runs, and Windows randomises the exe's address
once per boot (MoveImages=0 does not stop it on Windows 10). A second copy started while the
first still runs maps the same image at the same address, so the address comes from a first
copy that is left open:

  ./qemu_vm.sh run                                    # the VM waits, paused
  python3 -I qemu_capture.py <exe> capture.bin        # boots it; stop this at the desktop

  in the guest: start the exe, leave its Steam error open, read its address:
    powershell -noexit -c "(Get-Process Rocksmith2014).MainModule.BaseAddress"

  python3 -I qemu_capture.py --base <address> <exe> capture.bin
  in the guest: start the exe again; the capture stops the guest by itself

The watchpoint is on a virtual address, so any process that writes there stops the guest.
A hit counts only in user mode, with this exe's PE header at the ImageBase; anything else
is resumed. QEMU stops after the writing instruction, so the state is a consistent one.

The output is a gzipped pickle that make_seed.py --from-gdb reads. It holds every resident
user page of the process, Windows DLLs included: keep it local, it is not the seed.
"""
import argparse, gzip, hashlib, os, pickle, re, socket, struct, sys

PAGE = 0x1000
REGS = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "eip", "eflags",
        "cs", "ss", "ds", "es", "fs", "gs")   # the stub's i386 order
USER_TOP = 0x80000000


class Stub:
    """The client side of the GDB remote protocol, as much of it as a capture needs."""

    def __init__(self, port, host="127.0.0.1"):
        self.sock = socket.create_connection((host, port))
        self.buf = b""
        self.ask("?")   # also clears the watchpoints of an earlier connection
        reply = self.ask("qSupported:hwbreak+")
        size = re.search(r"PacketSize=([0-9a-fA-F]+)", reply)
        # a memory reply is hex: two characters per byte, and some room for the framing
        self.chunk = min(PAGE, (int(size.group(1), 16) - 32) // 2) if size else 0x400

    def _byte(self):
        while not self.buf:
            data = self.sock.recv(65536)
            if not data:
                raise ConnectionError("QEMU closed the connection: did the VM shut down?")
            self.buf = data
        b, self.buf = self.buf[:1], self.buf[1:]
        return b

    def send(self, payload):
        data = payload.encode("latin-1")
        self.sock.sendall(b"$%s#%02x" % (data, sum(data) & 0xff))
        while self._byte() != b"+":
            pass

    def recv(self):
        while self._byte() != b"$":
            pass
        out = bytearray()
        while (b := self._byte()) != b"#":
            out += b
        self._byte(); self._byte()
        self.sock.sendall(b"+")
        return out.decode("latin-1")

    def ask(self, payload):
        self.send(payload)
        return self.recv()

    def read(self, addr, size):
        """Guest virtual memory in the current process, or None where it is not mapped."""
        out = bytearray()
        while size:
            n = min(size, self.chunk)
            reply = self.ask("m%x,%x" % (addr, n))
            if not reply or reply[0] == "E":
                return None
            out += bytes.fromhex(reply)
            addr += n
            size -= n
        return bytes(out)

    def regs(self):
        raw = bytes.fromhex(self.ask("g"))
        return dict(zip(REGS, struct.unpack_from("<16I", raw)))

    def monitor(self, command):
        self.send("qRcmd," + command.encode().hex())
        out = bytearray()
        while True:
            reply = self.recv()
            if reply.startswith("O") and reply != "OK":
                out += bytes.fromhex(reply[1:])
            else:
                return out.decode("latin-1")

    def watch(self, addr, size=4):
        if self.ask("Z2,%x,%x" % (addr, size)) != "OK":
            raise RuntimeError("the stub refused a write watchpoint on 0x%x" % addr)

    def resume(self):
        """Run the guest to its next watchpoint hit and return the watched address."""
        reply = self.ask("c")
        if reply[:1] in ("W", "X"):
            raise ConnectionError("the guest ended: " + reply)
        hit = re.search(r"watch:([0-9a-fA-F]+)", reply)
        if not hit:
            raise ConnectionError("the guest stopped by itself: " + reply)
        return int(hit.group(1), 16)


def exe_layout(path):
    """What the capture needs from the PE header of the exe on disk."""
    with open(path, "rb") as f:
        head = f.read(0x1000)
    pe = struct.unpack_from("<I", head, 0x3c)[0]
    if head[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", head, pe + 24)[0] != 0x10b:
        raise ValueError("not a PE32 exe: " + path)
    nsec, = struct.unpack_from("<H", head, pe + 6)
    opt_size, = struct.unpack_from("<H", head, pe + 20)
    lay = {"pe": pe, "stamp": struct.unpack_from("<I", head, pe + 8)[0],
           "base": struct.unpack_from("<I", head, pe + 24 + 28)[0],
           "size": struct.unpack_from("<I", head, pe + 24 + 56)[0]}
    for i in range(nsec):
        sec = pe + 24 + opt_size + 40 * i
        if head[sec:sec + 8].rstrip(b"\0") == b".text":
            lay["text_rva"], = struct.unpack_from("<I", head, sec + 12)
            return lay
    raise ValueError("no .text section: " + path)


def is_our_exe(stub, lay):
    """The stop is ours if the process on the CPU has this exe's header at the ImageBase."""
    head = stub.read(lay["base"], PAGE)
    if head is None or head[:2] != b"MZ" or head[0x3c:0x40] != struct.pack("<I", lay["pe"]):
        return False
    stamp, = struct.unpack_from("<I", head, lay["pe"] + 8)
    size, = struct.unpack_from("<I", head, lay["pe"] + 24 + 56)
    return stamp == lay["stamp"] and size == lay["size"]


def wait_for_write(stub, addr, accept, log=print):
    """Resume until accept() takes a write to addr, and leave the guest stopped there."""
    stub.watch(addr)
    skipped = 0
    while True:
        if stub.resume() == addr and accept():
            return
        skipped += 1
        log("[capture] stop %d was not the exe, resuming" % skipped)


def cpu_state(stub):
    """What the register packet leaves out: GDT, CR3 and the segment bases."""
    text = stub.monitor("info registers")
    gdt = re.search(r"GDT=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", text)
    cr3 = re.search(r"CR3=([0-9a-fA-F]+)", text)
    if not gdt or not cr3:
        raise RuntimeError("no GDT or CR3 in the monitor's output:\n" + text)
    bases = {m.group(1).lower(): int(m.group(2), 16)
             for m in re.finditer(r"^(FS|GS)\s*=\s*[0-9a-fA-F]+\s+([0-9a-fA-F]+)", text, re.M)}
    return {"gdt_addr": int(gdt.group(1), 16), "gdt_limit": int(gdt.group(2), 16),
            "cr3": int(cr3.group(1), 16), "seg_bases": bases}


def user_ranges(stub):
    """The user-mode mappings of the current process, from the monitor's page table walk."""
    ranges = []
    for lo, hi, flags in re.findall(r"^([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(\S+)",
                                    stub.monitor("info mem"), re.M):
        lo, hi = int(lo, 16), int(hi, 16)
        if flags.startswith("u") and lo < USER_TOP:
            ranges.append((lo, min(hi, USER_TOP)))
    return ranges


def dump(stub, lay, exe_sha256, log=print):
    regs = stub.regs()
    state = cpu_state(stub)
    gdt = stub.read(state["gdt_addr"], state["gdt_limit"] + 1)
    if gdt is None:
        raise RuntimeError("the GDT is not readable")

    base, end = lay["base"], lay["base"] + lay["size"]
    image, pages = {}, []
    for lo, hi in user_ranges(stub):
        for addr in range(lo, hi, PAGE):
            data = stub.read(addr, PAGE)
            if data is None:
                continue
            if base <= addr < end:
                image[addr - base] = data
            else:
                pages.append((addr, data))
    log("[capture] %d image pages, %d other user pages" % (len(image), len(pages)))
    return {"format": "qemu_gdb_capture_v2", "image_base": base, "image_size": lay["size"],
            "regs": regs, "gdt": gdt, "image_pages": image, "pages": pages,
            "exe_sha256": exe_sha256, **state}


def main():
    ap = argparse.ArgumentParser(description="Capture VMProtect's state from a QEMU guest")
    ap.add_argument("exe", help="the Rocksmith2014.exe on the host, the same file as the guest's")
    ap.add_argument("output", help="capture file to write")
    ap.add_argument("--port", type=int, default=1234, help="QEMU's GDB stub port")
    ap.add_argument("--base", type=lambda v: int(v, 0),
                    help="where the guest loads the exe, if not at its ImageBase")
    args = ap.parse_args()

    lay = exe_layout(args.exe)
    lay["base"] = args.base or lay["base"]
    with open(args.exe, "rb") as f:
        exe_sha256 = hashlib.sha256(f.read()).hexdigest()
    text = lay["base"] + lay["text_rva"]

    stub = Stub(args.port)
    print("[capture] watching .text at 0x%08x; the guest is running" % text)
    print("[capture] start the exe in the guest; if it gets to its Steam error with no "
          "capture, it did not load at 0x%08x" % lay["base"])

    def accept():
        return stub.regs()["cs"] & 3 == 3 and is_our_exe(stub, lay)
    wait_for_write(stub, text, accept)
    print("[capture] decompression started, EIP 0x%08x; reading the guest" % stub.regs()["eip"])

    capture = dump(stub, lay, exe_sha256)
    with gzip.open(args.output, "wb") as f:
        pickle.dump(capture, f, protocol=4)
    print("[capture] wrote %s (%d KB)" % (args.output, os.path.getsize(args.output) // 1024))
    print("[capture] the guest is left stopped; next: python3 -I make_seed.py --from-gdb "
          "%s %s seed.bin" % (args.output, args.exe))


if __name__ == "__main__":
    try:
        main()
    except (ConnectionError, RuntimeError, ValueError, OSError) as e:
        sys.exit("qemu_capture: %s" % e)
