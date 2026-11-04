using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

// Rocksmith2014 page harvester, version 2.
// Leave it running while you play the game. VMProtect decrypts code pages only
// while they are executing and re-encrypts them when idle, so this keeps reading
// the game's memory and saves the first real (non-zero) copy of each page it sees.
// The more of the game you move through, the more code it captures.
//
// v2: reads the game from wherever Windows actually loaded it, for its whole size,
// and also records every loaded module and its exports, so the game's imports can
// be named afterwards.
//
// Output, next to this exe:
//   rs_harvest2.bin       the game image by RVA, first non-zero copy of each page
//   rs_harvest2_last.bin  the game image by RVA, latest non-zero copy of each page
//   rs_modules.tsv        every module seen: name, base, size, path
//   rs_exports.tsv        every export of those modules: module, ordinal, name, address, forwarder
//   rs_harvest2.txt       game base, size, exe path, exe size, exe SHA-256, page counts

class Program
{
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool K32EnumProcessModulesEx(IntPtr h, [Out] IntPtr[] mods, uint cb, out uint needed, uint filter);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool K32GetModuleInformation(IntPtr h, IntPtr mod, out MODULEINFO info, uint cb);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern uint K32GetModuleFileNameExW(IntPtr h, IntPtr mod, StringBuilder name, uint size);

    [StructLayout(LayoutKind.Sequential)]
    struct MODULEINFO { public IntPtr lpBaseOfDll; public uint SizeOfImage; public IntPtr EntryPoint; }

    class Module { public string Name = "", Path = ""; public ulong Base; public uint Size; public bool ExportsRead; }

    const int PG = 0x1000;

    static volatile bool stop = false;
    static IntPtr h;
    static readonly Dictionary<ulong, Module> modules = new();
    static readonly List<string> exportLines = new();

    static void Main()
    {
        Console.WriteLine("=== Rocksmith2014 page harvester v2 ===");
        Console.WriteLine("1) Start the game and get to the main menu first.");
        Console.WriteLine("2) Then leave this window open and PLAY / move around the game.");
        Console.WriteLine("3) Close the game (or press Ctrl+C here) when done. It saves the rs_* files.\n");

        Console.CancelKeyPress += (s, e) => { e.Cancel = true; stop = true; };

        Process? p = null;
        while (p == null && !stop)
        {
            var ps = Process.GetProcessesByName("Rocksmith2014");
            if (ps.Length > 0) p = ps[0];
            else { Console.WriteLine("waiting for Rocksmith2014.exe ..."); System.Threading.Thread.Sleep(2000); }
        }
        if (p == null) return;
        Console.WriteLine("found Rocksmith2014 (pid " + p.Id + ")");

        h = OpenProcess(0x0410, false, p.Id); // PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
        if (h == IntPtr.Zero) { Console.WriteLine("could not open the game process (error " + Marshal.GetLastWin32Error() + ")."); Pause(); return; }

        // The game module: wait until the loader has it listed.
        Module? game = null;
        while (game == null && !stop && !p.HasExited)
        {
            RefreshModules();
            foreach (var m in modules.Values)
                if (m.Name.Equals("Rocksmith2014.exe", StringComparison.OrdinalIgnoreCase)) game = m;
            if (game == null) System.Threading.Thread.Sleep(500);
        }
        if (game == null) { Console.WriteLine("could not find the game module."); Pause(); return; }

        string exeSha = "", exeSize = "";
        try
        {
            byte[] exe = File.ReadAllBytes(game.Path);
            exeSize = exe.Length.ToString();
            using (var sha = SHA256.Create())
                exeSha = BitConverter.ToString(sha.ComputeHash(exe)).Replace("-", "").ToLowerInvariant();
        }
        catch (Exception e) { Console.WriteLine("(could not hash the exe: " + e.Message + ")"); }

        Console.WriteLine(string.Format("game loaded at 0x{0:x}, size 0x{1:x} - harvesting...\n", game.Base, game.Size));

        int isz = (int)game.Size;
        int pgcount = isz / PG;
        byte[] img = new byte[isz];
        byte[] last = new byte[isz];
        bool[] got = new bool[pgcount];
        byte[] pg = new byte[PG];
        int captured = 0;
        var sw = Stopwatch.StartNew();
        double lastPrint = -5, lastSave = 0, lastFull = -100;
        string dir = AppContext.BaseDirectory;
        string binPath = Path.Combine(dir, "rs_harvest2.bin");
        string lastPath = Path.Combine(dir, "rs_harvest2_last.bin");
        string infoPath = Path.Combine(dir, "rs_harvest2.txt");

        // Resume: keep the pages of a previous run of the same exe and add to them,
        // so coverage accumulates across several play sessions.
        if (File.Exists(binPath))
        {
            try
            {
                bool same = File.Exists(infoPath) && exeSha != "" && File.ReadAllText(infoPath).Contains("exe_sha256\t" + exeSha);
                byte[] prev = File.ReadAllBytes(binPath);
                if (same && prev.Length == isz)
                {
                    Array.Copy(prev, img, isz);
                    if (File.Exists(lastPath))
                    {
                        byte[] prevLast = File.ReadAllBytes(lastPath);
                        if (prevLast.Length == isz) Array.Copy(prevLast, last, isz);
                    }
                    for (int i = 0; i < pgcount; i++)
                        if (!IsZero(img, i * PG)) { got[i] = true; captured++; }
                    Console.WriteLine("resumed from existing rs_harvest2.bin: " + captured + " pages already captured.\n");
                }
                else
                {
                    string old = binPath + ".old-" + DateTime.Now.ToString("yyyyMMdd-HHmmss");
                    File.Move(binPath, old);
                    Console.WriteLine("existing rs_harvest2.bin is from a different game build; moved it to " + Path.GetFileName(old) + "\n");
                }
            }
            catch { }
        }

        while (!stop)
        {
            if (p.HasExited) { Console.WriteLine("\ngame closed."); break; }
            double t = sw.Elapsed.TotalSeconds;

            // Every 10 s: re-read every page into the "latest" copy (this catches import
            // slots and data as the game fills them in) and look for newly loaded modules.
            bool full = t - lastFull >= 10;
            for (int i = 0; i < pgcount; i++)
            {
                if (got[i] && !full) continue;
                IntPtr rd;
                if (!ReadProcessMemory(h, (IntPtr)(long)(game.Base + (ulong)i * PG), pg, (IntPtr)PG, out rd)) continue;
                if (IsZero(pg, 0)) continue;
                Array.Copy(pg, 0, last, i * PG, PG);
                if (!got[i]) { Array.Copy(pg, 0, img, i * PG, PG); got[i] = true; captured++; }
            }
            if (full) { RefreshModules(); lastFull = t; }

            if (t - lastPrint >= 5)
            {
                Console.WriteLine(string.Format("  {0,4:0}s   captured {1}/{2} pages  ({3}%)   {4} modules   <- keep playing to raise this",
                    t, captured, pgcount, 100 * captured / pgcount, modules.Count));
                lastPrint = t;
            }
            if (t - lastSave >= 10) { SaveAll(dir, img, last, game, exeSha, exeSize, captured, pgcount); lastSave = t; }
            System.Threading.Thread.Sleep(40);
        }

        SaveAll(dir, img, last, game, exeSha, exeSize, captured, pgcount);
        Console.WriteLine("\nSAVED in " + dir);
        Console.WriteLine(string.Format("captured {0}/{1} pages ({2}%), {3} modules, {4} exports.",
            captured, pgcount, 100 * captured / pgcount, modules.Count, exportLines.Count));
        Console.WriteLine("Send back these five files: rs_harvest2.bin, rs_harvest2_last.bin, rs_modules.tsv, rs_exports.tsv, rs_harvest2.txt");
        Pause();
    }

    static bool IsZero(byte[] b, int off)
    {
        for (int k = 0; k < PG; k++) if (b[off + k] != 0) return false;
        return true;
    }

    static void RefreshModules()
    {
        var mods = new IntPtr[2048];
        if (!K32EnumProcessModulesEx(h, mods, (uint)(mods.Length * IntPtr.Size), out uint needed, 0x03)) return;
        int n = Math.Min(mods.Length, (int)(needed / IntPtr.Size));
        for (int i = 0; i < n; i++)
        {
            ulong b = (ulong)(long)mods[i];
            if (modules.ContainsKey(b)) continue;
            if (!K32GetModuleInformation(h, mods[i], out MODULEINFO mi, (uint)Marshal.SizeOf<MODULEINFO>())) continue;
            var sb = new StringBuilder(1024);
            K32GetModuleFileNameExW(h, mods[i], sb, (uint)sb.Capacity);
            var m = new Module { Base = b, Size = mi.SizeOfImage, Path = sb.ToString() };
            m.Name = Path.GetFileName(m.Path);
            modules[b] = m;
        }
        foreach (var m in modules.Values)
            if (!m.ExportsRead) { ReadExports(m); m.ExportsRead = true; }
    }

    static byte[]? Read(ulong addr, int len)
    {
        var buf = new byte[len];
        return ReadProcessMemory(h, (IntPtr)(long)addr, buf, (IntPtr)len, out IntPtr rd) && (long)rd == len ? buf : null;
    }

    static string ReadString(ulong addr)
    {
        var sb = new StringBuilder();
        for (int chunk = 0; chunk < 4; chunk++)
        {
            var b = Read(addr + (ulong)(chunk * 64), 64);
            if (b == null) break;
            foreach (byte c in b) { if (c == 0) return sb.ToString(); sb.Append((char)c); }
        }
        return sb.ToString();
    }

    static void ReadExports(Module m)
    {
        try
        {
            var hdr = Read(m.Base, PG);
            if (hdr == null || hdr[0] != 'M' || hdr[1] != 'Z') return;
            int pe = BitConverter.ToInt32(hdr, 0x3c);
            if (pe < 0 || pe > PG - 0x100) return;
            int opt = pe + 24;
            ushort magic = BitConverter.ToUInt16(hdr, opt);
            int dd = opt + (magic == 0x20b ? 112 : 96);
            uint erva = BitConverter.ToUInt32(hdr, dd), esz = BitConverter.ToUInt32(hdr, dd + 4);
            if (erva == 0 || esz == 0) return;
            var ed = Read(m.Base + erva, 40);
            if (ed == null) return;
            uint ordBase = BitConverter.ToUInt32(ed, 16);
            int nfun = BitConverter.ToInt32(ed, 20), nnam = BitConverter.ToInt32(ed, 24);
            uint afun = BitConverter.ToUInt32(ed, 28), anam = BitConverter.ToUInt32(ed, 32), aord = BitConverter.ToUInt32(ed, 36);
            if (nfun <= 0 || nfun > 65536 || nnam < 0 || nnam > 65536) return;
            var funs = Read(m.Base + afun, nfun * 4);
            if (funs == null) return;
            var names = new Dictionary<int, string>();
            if (nnam > 0)
            {
                var nptr = Read(m.Base + anam, nnam * 4);
                var nord = Read(m.Base + aord, nnam * 2);
                if (nptr != null && nord != null)
                    for (int i = 0; i < nnam; i++)
                        names[BitConverter.ToUInt16(nord, i * 2)] = ReadString(m.Base + BitConverter.ToUInt32(nptr, i * 4));
            }
            for (int i = 0; i < nfun; i++)
            {
                uint rva = BitConverter.ToUInt32(funs, i * 4);
                if (rva == 0) continue;
                string fwd = rva >= erva && rva < erva + esz ? ReadString(m.Base + rva) : "";
                names.TryGetValue(i, out string? name);
                exportLines.Add(string.Format("{0}\t{1}\t{2}\t0x{3:x}\t{4}", m.Name, ordBase + (uint)i, name ?? "", m.Base + rva, fwd));
            }
        }
        catch { }
    }

    static void SaveAll(string dir, byte[] img, byte[] last, Module game, string sha, string size, int captured, int pgcount)
    {
        Save(Path.Combine(dir, "rs_harvest2.bin"), img);
        Save(Path.Combine(dir, "rs_harvest2_last.bin"), last);
        var ms = new StringBuilder("name\tbase\tsize\tpath\n");
        foreach (var m in modules.Values)
            ms.AppendFormat("{0}\t0x{1:x}\t0x{2:x}\t{3}\n", m.Name, m.Base, m.Size, m.Path);
        SaveText(Path.Combine(dir, "rs_modules.tsv"), ms.ToString());
        SaveText(Path.Combine(dir, "rs_exports.tsv"), "module\tordinal\tname\taddress\tforwarder\n" + string.Join("\n", exportLines) + "\n");
        SaveText(Path.Combine(dir, "rs_harvest2.txt"), string.Format(
            "harvester\t2\ngame_base\t0x{0:x}\ngame_size\t0x{1:x}\nexe_path\t{2}\nexe_size\t{3}\nexe_sha256\t{4}\npages_captured\t{5}\npages_total\t{6}\n",
            game.Base, game.Size, game.Path, size, sha, captured, pgcount));
    }

    static void Save(string path, byte[] data)
    {
        try { File.WriteAllBytes(path, data); } catch { }
    }
    static void SaveText(string path, string s)
    {
        try { File.WriteAllText(path, s); } catch { }
    }
    static void Pause()
    {
        Console.WriteLine("\nPress Enter to close.");
        try { Console.ReadLine(); } catch { }
    }
}
