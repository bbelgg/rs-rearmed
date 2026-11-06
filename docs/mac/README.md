# Mac port

RS Re-ARMed for the Steam macOS version of Rocksmith 2014 (Remastered): the x86-64 game, which would
otherwise run under Rosetta, recompiled into a native arm64 app. Released; `VERSION` says which release this is.

| Document | For | What |
|---|---|---|
| [INSTALL.md](INSTALL.md) | players | requirements, the installer, the manual build, playing, switches, known issues, troubleshooting |
| [CAPTURE.md](CAPTURE.md) | players | why the Mac edition needs no capture, and what it does instead |
| [DESIGN.md](DESIGN.md) | developers | how the port works: the game image, translation, the tables, the shell, audio, Steam, online, mods |
| [METAL.md](METAL.md) | developers | the renderer: the GL-on-Metal layer, defaults and fallback, shader corpus, safe load, status |
| [PERFORMANCE.md](PERFORMANCE.md) | both | CPU and frame times against Rosetta, Metal against OpenGL, build options |
| [PLAN.md](PLAN.md) | developers | status, open issues, next work |
| [TESTING.md](TESTING.md) | developers | the tests in the repository, installer tests, game runs, diagnostics, offline checks |

The shared recompiler: [docs/recompiler](../recompiler/README.md). The Windows port, for side-by-side
reading (the same headings where the topics match): [docs/windows](../windows/README.md).
How the two games differ: [EDITIONS.md](../EDITIONS.md).
