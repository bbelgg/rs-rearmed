#!/usr/bin/env python3
"""The key of the game's archives, read from the player's own game.

A .psarc archive's table of contents is AES-256-CFB encrypted with one key, the same in every archive of both
editions. The key is in the game: its code image holds it as a line of 64 hex digits. Nothing of it is in this
repository; this module finds it in a game image and knows it by its SHA-256.

    psarc_key.py IMAGE [GAME_COPY]    say whether IMAGE holds the key; with GAME_COPY, note it there

Where the key comes from, in order (toc_key):
  1. $RS_PSARC_KEY_IMAGE: a game image (the Mac game's executable, decrypted or as Steam has it, or Learn &
     Play's unpacked code image; Learn & Play's exe as Steam has it is packed and does not show it).
  2. Near the archive being read, in its folder and the three above it:
       .rearmed/toc-key                                            the note a copy of the game files keeps (note)
       Rocksmith2014.app/Contents/MacOS/Rocksmith2014              the Mac game, as Steam has it
       Rocksmith2014.app/Contents/Resources/Rocksmith2014.x86_64   RS Re-ARMed's Mac app
So a Mac game folder and either edition's copy of the game files need nothing set; an archive anywhere else
(a download folder of Learn & Play, a song on the desktop) needs RS_PSARC_KEY_IMAGE.

The table's IV is not needed to read an archive: in CFB only the first block depends on it, and that block is
entry 0's name digest, which nothing reads. To write an archive the game will read, the IV is worked out from an
archive of the game (table_iv): entry 0's digest is sixteen zero bytes, so the first block of the encrypted table
is the IV encrypted with the key.

Standard library only; AES is done with /usr/bin/openssl. Next to gamefiles.py and cache_overlay.py in the
repository and in the installs.
"""
import hashlib
import mmap
import os
import re
import subprocess
import sys

KEY_SHA256 = "a626409bbdc4fd901f065e868df1eac0a53ee7540a8d3010823a6860b71a01ef"  # of the 64 digits, as text
IV_SHA256 = "e0301bf15e15c05e16307e7c2d5c23fa78d2db3ab2daf47b3c11114bd3ea4b27"   # of the 16 bytes
IMAGE_ENV = "RS_PSARC_KEY_IMAGE"
META = ".rearmed"       # gamefiles.py's folder in a copy of the game files
NOTE = "toc-key"
IMAGES = (os.path.join("Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014"),
          os.path.join("Rocksmith2014.app", "Contents", "Resources", "Rocksmith2014.x86_64"))
UP = 3  # folders above the archive's own that are looked in: <copy>/dlc/cdlc/pack/x.psarc finds <copy>

_HEX64 = re.compile(rb"(?<![0-9A-F])[0-9A-F]{64}(?![0-9A-F])")
_key = None
_iv = None
_checked = True  # False while a test's own key is in use (use)


class NoKey(Exception):
    """The key could not be found: the message says where it was looked for."""


def use(key, iv):
    """A key and an IV of the caller's own (hex), for tests with archives made here; None, None ends it."""
    global _key, _iv, _checked
    _key, _iv, _checked = key, iv, key is None


def _is_key(text):
    return hashlib.sha256(text).hexdigest() == KEY_SHA256


def key_in_image(path):
    """The key (64 hex digits, str) a game image holds, or None."""
    try:
        with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
            for found in _HEX64.finditer(m):
                if _is_key(found.group()):
                    return found.group().decode()
    except (OSError, ValueError):  # unreadable, or empty
        pass
    return None


def _noted(folder):
    try:
        with open(os.path.join(folder, META, NOTE), "rb") as f:
            text = f.read(200).strip()
    except OSError:
        return None
    return text.decode() if _is_key(text) else None


def note(copy, key=None, image=None):
    """Keep the key in COPY (a copy of the game files), so its archives can be read wherever its game is.
    From KEY or from IMAGE; True when the copy has it afterwards."""
    key = key or (key_in_image(image) if image else None)
    if not key or not _is_key(key.encode()):
        return _noted(copy) is not None
    if _noted(copy) != key:
        os.makedirs(os.path.join(copy, META), exist_ok=True)
        path = os.path.join(copy, META, NOTE)
        with open(path + ".part", "w") as f:
            f.write(key + "\n")
        os.replace(path + ".part", path)
    return True


def _folders(near):
    out = []
    for start in near:
        d = os.path.abspath(start)
        if not os.path.isdir(d):
            d = os.path.dirname(d)
        for _ in range(UP + 1):
            if d not in out:
                out.append(d)
            d = os.path.dirname(d)
    return out


def find_key(*near):
    """The key for an archive at (or a folder at) any of NEAR, or None; see the top of this file."""
    image = os.environ.get(IMAGE_ENV)
    if image:
        key = key_in_image(image)
        if key:
            return key
    for d in _folders(near):
        key = _noted(d)
        if key:
            return key
        for rel in IMAGES:
            p = os.path.join(d, rel)
            if os.path.isfile(p):
                key = key_in_image(p)
                if key:
                    return key
    return None


def toc_key(*near):
    """The key (hex), found once in a run. NoKey when the game it comes from is not at hand."""
    global _key
    if _key is None:
        _key = find_key(*near)
    if _key is None:
        where = ", ".join(n for n in near if n) or "no archive named"
        raise NoKey("the archives' key was not found (%s): it is read from the game's own code, so set %s to "
                    "the game's image (the Mac game's executable, or Learn & Play's unpacked code image)"
                    % (where, IMAGE_ENV))
    return _key


def have_key(*near):
    try:
        toc_key(*near)
        return True
    except NoKey:
        return False


def _aes(args, data):
    return subprocess.run(["openssl", "enc"] + args + ["-nopad"], input=data, capture_output=True,
                          check=True).stdout


def crypt_toc(data, decrypt, key, iv=None):
    """A table of contents de- or encrypted. Decrypting needs no IV (the first 16 bytes come out as zeros,
    which entry 0's digest is)."""
    if not data:
        return data
    padded = data + b"\0" * (-len(data) % 16)  # CFB is a stream mode, but openssl wants whole blocks
    out = _aes(["-d" if decrypt else "-e", "-aes-256-cfb", "-K", key, "-iv", iv or "00" * 16], padded)[:len(data)]
    return bytes(16) + out[16:] if decrypt and iv is None else out


def table_iv(first_block, key):
    """Note the IV from FIRST_BLOCK, the first 16 bytes of an encrypted table of contents of a game archive;
    False when the block is not one (an archive some tool wrote with another IV)."""
    global _iv
    if _iv is not None or len(first_block) != 16:
        return _iv is not None
    iv = _aes(["-d", "-aes-256-ecb", "-K", key], first_block)
    if not _checked or hashlib.sha256(iv).hexdigest() == IV_SHA256:
        _iv = iv.hex().upper()
    return _iv is not None


def toc_iv():
    """The IV for writing a table of contents; NoKey until an encrypted game archive was read (table_iv)."""
    if _iv is None:
        raise NoKey("the archives' IV is not known yet: it is worked out from one of the game's own archives, "
                    "read first")
    return _iv


def main(argv):
    if len(argv) not in (2, 3):
        sys.exit(__doc__.split("\n\n")[2].strip("\n"))
    key = key_in_image(argv[1])
    if not key:
        sys.exit("%s does not hold the archives' key (not a game image, or Learn & Play's packed exe)" % argv[1])
    print("%s holds the archives' key" % argv[1])
    if len(argv) == 3:
        note(argv[2], key)
        print("noted in %s" % os.path.join(argv[2], META, NOTE))


if __name__ == "__main__":
    main(sys.argv)
