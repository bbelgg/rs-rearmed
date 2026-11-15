# Mac port

RS Re-ARMed for the Steam macOS version of Rocksmith 2014 (Remastered): the x86-64 game, which would
otherwise run under Rosetta, recompiled into a native arm64 app. Released; `VERSION` says which release this is.

| Document | For | What |
|---|---|---|
| [INSTALL.md](INSTALL.md) | players | requirements, the installer, the manual build, game files, playing, switches, known issues, troubleshooting |
| [CAPTURE.md](CAPTURE.md) | builders | why the Mac edition needs no capture, and what it does instead |
| [DESIGN.md](DESIGN.md) | developers | how the port works: the game image, translation, the tables, the shell, the renderer, audio, Steam, online, mods, the installer |
| [METAL.md](METAL.md) | developers | the renderer both editions draw through: the GL-on-Metal layer, defaults and fallback, safe load, the shader corpus, the overlay and render APIs |
| [PERFORMANCE.md](PERFORMANCE.md) | both | CPU and frame times against Rosetta, Metal against OpenGL, build options |
| [PLAN.md](PLAN.md) | developers | status, open issues, next work, risks, decisions |
| [TESTING.md](TESTING.md) | developers | the tests in the repository, installer tests, game runs, diagnostics, offline checks, release checks |

The shared recompiler: [docs/recompiler](../recompiler/README.md). The Windows port, for side-by-side
reading (the same headings where the topics match): [docs/windows](../windows/README.md).
How the two games differ: [EDITIONS.md](../EDITIONS.md).
