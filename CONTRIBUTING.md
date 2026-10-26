# Contributing

Issues and pull requests are welcome.

## Keep game content out

RS Re-ARMed ships no game code or assets, and every commit has to keep it that way. Never commit:

- game files of any kind (archives, audio, video, textures, the binary, icons, saves);
- decrypted or otherwise extracted game binaries;
- generated C, objects or anything else from a build directory;
- logs, screenshots or dumps that contain game content beyond what is needed to describe a bug.

If a change needs something from the game, read it from the user's install at build time.

## Code

- Follow the style of the surrounding code.
- Keep each commit to one change, with a message that says what it does and why.
- Before reporting a bug in the port, check that it does not also happen in the original game under
  Rosetta.

## Licence

RS Re-ARMed is MIT-licensed (see `LICENSE`). By contributing, you agree that your contributions are
licensed under the MIT licence.
