"""Tests for the QEMU capture, against a fake GDB stub: no QEMU or guest needed."""
import os, socket, struct, sys, tempfile, threading, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qemu_capture


class FakeStub(threading.Thread):
    """Answers each packet from a table; "c" takes the next of a list of stop replies."""

    def __init__(self, stops=(), memory=None, monitor=""):
        super().__init__(daemon=True)
        self.server = socket.create_server(("127.0.0.1", 0))
        self.port = self.server.getsockname()[1]
        self.stops, self.memory, self.monitor = list(stops), memory or {}, monitor
        self.packets = []
        self.start()

    def reply(self, packet):
        if packet.startswith("qSupported"):
            return ["PacketSize=2020"]
        if packet == "?" or packet.startswith("Z2"):
            return ["OK" if packet != "?" else "T05"]
        if packet == "c":
            return [self.stops.pop(0)]
        if packet == "g":
            return [struct.pack("<16I", *range(16)).hex()]
        if packet.startswith("qRcmd"):
            return ["O" + self.monitor.encode().hex(), "OK"]
        if packet.startswith("m"):
            addr, size = (int(v, 16) for v in packet[1:].split(","))
            data = self.memory.get(addr)
            return [data[:size].hex() if data else "E14"]
        return [""]

    def run(self):
        conn, _ = self.server.accept()
        data = b""
        while True:
            while b"#" not in data or len(data) < data.index(b"#") + 3:
                got = conn.recv(4096)
                if not got:
                    return
                data += got
            start, end = data.index(b"$"), data.index(b"#")
            packet = data[start + 1:end].decode()
            data = data[end + 3:]
            self.packets.append(packet)
            conn.sendall(b"+")
            for r in self.reply(packet):
                raw = r.encode()
                conn.sendall(b"$%s#%02x" % (raw, sum(raw) & 0xff))


def pe_head(stamp=7, size=0x5000):
    head = bytearray(0x1000)
    head[:2] = b"MZ"
    struct.pack_into("<I", head, 0x3c, 0x80)
    head[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", head, 0x86, 1)
    struct.pack_into("<I", head, 0x88, stamp)
    struct.pack_into("<H", head, 0x94, 0xe0)
    struct.pack_into("<H", head, 0x98, 0x10b)
    struct.pack_into("<I", head, 0x98 + 28, 0x400000)
    struct.pack_into("<I", head, 0x98 + 56, size)
    sec = 0x98 + 0xe0
    head[sec:sec + 5] = b".text"
    struct.pack_into("<I", head, sec + 12, 0x1000)
    return bytes(head)


class Layout(unittest.TestCase):
    def test_reads_the_pe_header(self):
        with tempfile.NamedTemporaryFile() as f:
            f.write(pe_head()); f.flush()
            lay = qemu_capture.exe_layout(f.name)
        self.assertEqual((lay["base"], lay["size"], lay["text_rva"], lay["stamp"]),
                         (0x400000, 0x5000, 0x1000, 7))


class Protocol(unittest.TestCase):
    def test_registers_in_the_stubs_order(self):
        regs = qemu_capture.Stub(FakeStub().port).regs()
        self.assertEqual((regs["eax"], regs["ecx"], regs["esp"], regs["eip"], regs["cs"]),
                         (0, 1, 4, 8, 10))

    def test_unmapped_memory_is_none(self):
        stub = qemu_capture.Stub(FakeStub(memory={0x1000: b"\x90" * 16}).port)
        self.assertEqual(stub.read(0x1000, 16), b"\x90" * 16)
        self.assertIsNone(stub.read(0x2000, 16))

    def test_user_ranges_leave_the_kernel_out(self):
        text = ("00010000-00020000 00010000 urw\r\n"
                "7ffe0000-7ffe1000 00001000 ur-\r\n"
                "80000000-80100000 00100000 -rw\r\n")
        stub = qemu_capture.Stub(FakeStub(monitor=text).port)
        self.assertEqual(qemu_capture.user_ranges(stub),
                         [(0x10000, 0x20000), (0x7ffe0000, 0x7ffe1000)])

    def test_cpu_state_from_the_monitor(self):
        text = ("FS =003b 03f44000 00000fff 0040f300 DPL=3\n"
                "GDT=     81131000 000003ff\nCR3=7ffdf600\n")
        state = qemu_capture.cpu_state(qemu_capture.Stub(FakeStub(monitor=text).port))
        self.assertEqual((state["gdt_addr"], state["gdt_limit"], state["cr3"]),
                         (0x81131000, 0x3ff, 0x7ffdf600))
        self.assertEqual(state["seg_bases"]["fs"], 0x3f44000)


class Waiting(unittest.TestCase):
    HIT = "T05thread:p01.01;watch:401000;"

    def test_other_processes_are_skipped(self):
        stub = qemu_capture.Stub(FakeStub(stops=[self.HIT] * 3).port)
        answers = iter([False, False, True])
        qemu_capture.wait_for_write(stub, 0x401000, lambda: next(answers), log=lambda _: None)
        self.assertEqual(next(answers, "none left"), "none left")

    def test_a_guest_that_stops_by_itself_is_an_error(self):
        stub = qemu_capture.Stub(FakeStub(stops=["T06thread:p01.01;"]).port)
        with self.assertRaises(ConnectionError):
            qemu_capture.wait_for_write(stub, 0x401000, lambda: True, log=lambda _: None)

    def test_only_this_exe_is_accepted(self):
        lay = {"base": 0x400000, "pe": 0x80, "stamp": 7, "size": 0x5000}
        ours = qemu_capture.Stub(FakeStub(memory={0x400000: pe_head()}).port)
        other = qemu_capture.Stub(FakeStub(memory={0x400000: pe_head(stamp=8)}).port)
        empty = qemu_capture.Stub(FakeStub().port)
        self.assertTrue(qemu_capture.is_our_exe(ours, lay))
        self.assertFalse(qemu_capture.is_our_exe(other, lay))
        self.assertFalse(qemu_capture.is_our_exe(empty, lay))


if __name__ == "__main__":
    unittest.main()
