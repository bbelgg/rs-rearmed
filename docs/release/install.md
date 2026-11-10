# The release notes' Install section

<!-- The same in every release. tools/release/bump.py fills in {version}. -->

This release contains only the installer. It contains no game code or assets: it downloads the v{version} source, builds
the game on your Mac from your own Steam copy and installs it. The built game can't be shared, so never download one
from anywhere.

You need an Apple Silicon Mac with macOS 12 or later, and Steam with Rocksmith 2014 Remastered (the Mac game) or
Rocksmith 2014 Learn & Play on your account. Apple's command line tools are the only other requirement, and the
installer starts their installation if they are missing.

1. Download `RS.Re-ARMed.Installer.app.zip` below, unzip it and open **RS Re-ARMed Installer**.
2. The installer is not notarized by Apple, so the first time macOS refuses to open it. Open **System Settings →
   Privacy & Security** and click **Open Anyway** next to the RS Re-ARMed Installer message.
3. Choose your game (Rocksmith 2014 for Mac, or Learn & Play) and click **Build & Install**. Whenever it needs you, a
   box above the log says what to do. It takes about 15 minutes for the Mac game, plus a few for Preparing shaders, and
   about 25 for Learn & Play, plus the download.
4. When it's done, start the game from the window the installer shows. Later, start it from `~/Applications`,
   Launchpad or Spotlight.

Updating from an earlier version: run this installer; your saves are kept. Built-in Mods… works once the game has been
installed by 1.11.0 or later. If you patched the game for another tool (for example RS Companion), install that again
afterwards. An earlier version may have left `static.psarc.rearmed` in your Steam game folder; you can delete it.

See the install guides for [the Mac game](https://github.com/bbelgg/rs-rearmed/blob/v{version}/docs/mac/INSTALL.md) and
[Learn & Play](https://github.com/bbelgg/rs-rearmed/blob/v{version}/docs/windows/INSTALL.md), and the
[README](https://github.com/bbelgg/rs-rearmed/blob/v{version}/README.md) for features and known issues.

Not affiliated with or endorsed by Ubisoft. Rocksmith is a trademark of Ubisoft.
