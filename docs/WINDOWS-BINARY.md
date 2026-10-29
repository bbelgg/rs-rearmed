# Getting the Windows build of Rocksmith 2014 on a Mac

Rocksmith 2014 Edition Remastered is now free to play on Steam as **Learn & Play**. It kept the original
app ID, **221680**, but Steam lists it for Windows only, so the Mac Steam client will not install it.
Valve's command-line client, steamcmd, can download the Windows files on a Mac. Nothing here runs the
Windows game; the files are for analysis.

## 1. Add the game to your account

Open the game's Steam store page, sign in and click **Play Game** (or **Add to Library**). If your
account already owns Rocksmith 2014, it has the app already and this step can be skipped.

## 2. Install steamcmd

steamcmd is an Intel program, so Apple Silicon Macs need Rosetta:

```bash
softwareupdate --install-rosetta --agree-to-license
```

Download Valve's archive with `curl`:

```bash
mkdir -p ~/steamcmd && curl -sqL https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz | tar zxf - -C ~/steamcmd
```

Don't use the Homebrew cask (`brew install --cask steamcmd`). Its copy of `Breakpad.framework` fails the
signature check after steamcmd updates itself ("Failed to load steamconsole.dylib ... library load
disallowed by system policy"), and macOS calls the framework damaged if you re-sign it locally.

## 3. Download the Windows files

Quit the Steam app first; it shares its data folder with steamcmd. Then run, with your own Steam user
name and an absolute folder path:

```bash
~/steamcmd/steamcmd.sh +@sSteamCmdForcePlatformType windows +force_install_dir /path/to/rs-windows +login YOUR_STEAM_USER +app_update 221680 validate +quit
```

> **Warning: back up your Mac install first.** steamcmd has been seen to ignore `force_install_dir` and
> install into the Mac Steam library, `~/Library/Application Support/Steam/steamapps/common/Rocksmith2014`.
> The Windows files then replace the Mac game: the app bundle loses its binary and frameworks, and the
> core `.psarc` files become the Windows versions. Copy that folder somewhere safe before downloading.
> When the download finishes, check that the files are in your chosen folder and the Mac app is intact.
> If the Mac install was overwritten, use **Verify integrity of game files** in the Mac Steam app.

- The first run updates steamcmd itself, which takes a minute or two.
- steamcmd asks for your password and, with Steam Guard on, a code. The password prompt shows nothing
  as you type; `ERROR (Invalid Password)` usually means a typo there.
- The download is a few GB. It is done when the output ends with
  `Success! App '221680' fully installed.`

## Troubleshooting

- **`Failed to load steamconsole.dylib`, mentioning Breakpad** (with the `curl` copy): re-sign the framework
  and run again. The error message gives its exact path.

  ```bash
  codesign --force --deep --sign - ~/steamcmd/Frameworks/Breakpad.framework
  ```

- **`No subscription`**: the account doesn't have the app yet. Do step 1, wait a minute, run again.
- steamcmd's own log is in `~/steamcmd/logs/console_log.txt`.
