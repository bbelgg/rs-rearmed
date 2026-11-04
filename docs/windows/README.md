# Windows port (Learn & Play)

**Experimental.** RS Re-ARMed for Rocksmith 2014 **Learn & Play**, a purchased edition that Steam offers for
Windows only: its 32-bit Windows game, recompiled into a native arm64 Mac app. Songs, Score Attack, the
Guitarcade, Session Mode, Nonstop Play, lessons and the window modes work; it has had far less testing than the
Mac port ([PLAN.md](PLAN.md#status)).

| Document | For | What |
|---|---|---|
| [INSTALL.md](INSTALL.md) | players | requirements, the installer, the capture for a build without the seed, `learn-play.sh`, keys, window modes, switches, known issues, troubleshooting |
| [CAPTURE.md](CAPTURE.md) | players | getting the game image: the harvest on a Windows PC, and the seed capture for Mac-only users |
| [DESIGN.md](DESIGN.md) | developers | how the port works: the game image, the multi-image runtime and 4 GB window, the Win32 seam, D3D9 and D3DX, the CRT, audio, Bink, Steam, online, input, the installer |
| [PERFORMANCE.md](PERFORMANCE.md) | both | frame times, CPU per thread, hot spots, what was changed, options, the Mac port on the same Mac |
| [PLAN.md](PLAN.md) | developers | status, open issues, next work, risks, decisions |
| [TESTING.md](TESTING.md) | developers | the tests in the repository, unattended and hand-driven game runs, diagnostics switches |

Code: [win-x86/README.md](../../win-x86/README.md). The shared recompiler: [docs/recompiler](../recompiler/README.md).
The Mac port, for side-by-side reading (the same headings where the topics match): [docs/mac](../mac/README.md).
