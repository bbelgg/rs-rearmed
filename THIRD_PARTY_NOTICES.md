# Third-party notices

RS Re-ARMed's own code is under the MIT licence ([LICENSE](LICENSE)). The components below are bundled
in this repo or downloaded and built by it, and keep their own licences. No game code or assets are
included.

| Component | Version | Origin | Licence | In this repo |
|---|---|---|---|---|
| Lua + LNUM patch | Lua 5.1.4, LNUM `lua514-lnum-20090417` | https://www.lua.org/ftp/lua-5.1.4.tar.gz, http://lua-users.org/wiki/LnumPatch | MIT | `shims/game/third_party/lua-5.1.4-lnum` |
| libpng | 1.5.13 (altered) | https://github.com/pnggroup/libpng, tag v1.5.13 | libpng licence | `shims/game/third_party/libpng-1.5.13` |
| libjpeg | IJG 8d (15-Jan-2012) | https://www.ijg.org/files/jpegsrc.v8d.tar.gz | IJG licence | `shims/game/third_party/jpeg-8d` |
| FFmpeg + Bink 2 decoder patch | FFmpeg 7.1.2 | https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz (downloaded at build time) | LGPL-2.1-or-later | `shims/bink/build-ffmpeg.sh`, `shims/bink/third_party/ffmpeg-bink2.patch` |
| rs-mac-fixes (behaviour only) | commit `2a05c31` | https://github.com/glebb/rs-mac-fixes | MIT; no upstream code is included | `tools/ws7/recomp/overrides/mods/mods.c` |
| RSBypass (behaviour only) | — | https://github.com/aik002/RSBypass | upstream has no licence file; no upstream code is included | `tools/ws7/recomp/overrides/mods/mods.c` |

## Lua 5.1.4 + LNUM

Copyright (C) 1994-2008 Lua.org, PUC-Rio. MIT licence; full text in
`shims/game/third_party/lua-5.1.4-lnum/COPYRIGHT`. The LNUM patch (integer number mode, by Asko
Kauppi) is included as `lua514-lnum-20090417.patch` and applied to the sources; it is distributed
under the same MIT terms as Lua. Only the library sources are included (no `lua.c`/`luac.c`/`print.c`).

Our changes are marked `RS:` in the source (`luaconf.h`, `luaconf_internal.h`, `llex.c`, `lstrlib.c`,
`ltable.c`): the game's buffer and identifier sizes, x86 conversion semantics for out-of-range doubles,
and a bounds fix for large negative 64-bit table keys.

## libpng 1.5.13

Copyright (c) 1998-2012 Glenn Randers-Pehrson, (c) 1996-1997 Andreas Dilger, (c) 1995-1996 Guy Eric
Schalnat, Group 42, Inc. libpng licence; full text in `shims/game/third_party/libpng-1.5.13/LICENSE`
and `png.h`.

**This is an altered version**, not the original libpng distribution: it contains only the library
sources, uses `scripts/pnglibconf.h.prebuilt` as `pnglibconf.h`, and is built with `-include math.h`
for macOS.

## libjpeg 8d

This software is based in part on the work of the Independent JPEG Group.

Copyright (C) 1991-2012, Thomas G. Lane, Guido Vollbeding. IJG licence: see "LEGAL ISSUES" in
`shims/game/third_party/jpeg-8d/README`. Only the library sources are included, with `jmemnobs.c`
and `jconfig.txt` as `jconfig.h`.

## FFmpeg 7.1.2 + Bink 2 decoder

FFmpeg is not stored in this repo. `shims/bink/build-ffmpeg.sh` downloads the 7.1.2 release tarball
(SHA-256 pinned in the script), applies `shims/bink/third_party/ffmpeg-bink2.patch` and builds a
minimal static FFmpeg (Bink demuxer and Bink/Bink 2 decoders only; no GPL or non-free parts enabled).
The patch is the unmerged ffmpeg-devel Bink 2 decoder (Copyright (c) 2014 Konstantin Shishkov,
(c) 2019 Paul B Mahol), in the revision pinned by https://github.com/procrastinine/maclauncher for
FFmpeg 7.1.2.

Licence: GNU Lesser General Public License, version 2.1 or later,
https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

FFmpeg is linked only into the Bink shim, `libBink2Macx64.dylib`, a separate dynamic library. You
can rebuild it against a modified FFmpeg (edit `build-ffmpeg.sh` or the patch, then rebuild the
`bink2` target) and replace the dylib; nothing else links FFmpeg.

## rs-mac-fixes

https://github.com/glebb/rs-mac-fixes (MIT, commit `2a05c31`). Its fixes are reimplemented as native
overrides in `tools/ws7/recomp/overrides/mods/mods.c`; no upstream code is included. Credit for the
original goes to its authors.

## RSBypass

The RSBypass part of `tools/ws7/recomp/overrides/mods/mods.c` is an independent reimplementation of the
behaviour of RSBypass by aik002 (https://github.com/aik002/RSBypass), which lets custom songs load.
Credit for the original goes to aik002. Upstream has no licence file, and no upstream code is
included. It is on by default (`RS_MODS_BYPASS=0` turns it off).

## Not bundled

`tools/loadscreen` uses Pillow, numpy and etcpak from PyPI in a local virtualenv.

## Trademarks

Not affiliated with or endorsed by Ubisoft. Rocksmith is a trademark of Ubisoft. Other names are
trademarks of their respective owners and are used only to describe compatibility.
