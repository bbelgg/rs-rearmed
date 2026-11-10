# Making a release

One release covers both editions: one version, one tag, one installer. The steps that are the same every time are
scripts in `tools/release`, and what has to be built, tested and run is decided by what changed since the last
release, not by habit. With Claude Code, `/release` walks through all of it and stops before anything is sent.

## Between releases

Two files here are kept up to date as work lands, so a release doesn't have to reconstruct them:

- [next.md](next.md): an entry for each change a player or a mod's author will notice, written in the same piece of
  work as the change. It becomes the notes' "What's new".
- [known-issues.md](known-issues.md): the notes' "Known issues", edited when an issue opens or closes.

In both `PLAN.md` files, mark an entry that isn't out yet with `(not released)` or `(after 1.12.0, not released)`;
the release turns it into `(new in 1.13.0)`. [install.md](install.md) is the notes' "Install" section.

## The steps

| Step | Command | What it does |
|---|---|---|
| 1. What to check | `tools/release/scope.py` | maps the files changed since the last tag to editions, game builds, suites and game runs |
| 2. Suites | `tools/release/check.py` | runs those suites, several at a time; a line each and `checks.json` |
| 3. Game runs | `tools/release/smoke.sh --mac`, `--lp` | one run to the note highway, for the editions step 1 names |
| 4. The edits | `tools/release/bump.py 1.13.0` | `VERSION`, the issue template, the plans' marks, the notes and a commit message draft |
| 5. The commit | by hand | `Release 1.13.0: …`, finished from the draft |
| 6. The installer | `RS_LP_SEED=… tools/release/package.sh` | the installer with the seed, zipped, with its SHA-256 |
| 7. Ready? | `tools/release/publish.sh 1.13.0` | every check of the tree, the history, the notes and the zip; sends nothing |
| 8. Publish | `tools/release/publish.sh 1.13.0 --yes` | the tag, the push, a draft release, the upload compared by download, then published |

Everything the steps write goes to `build/release` (`$RS_RELEASE_OUT`), never into the tree. Steps 2, 3 and 6
build or run, so they refuse to start while a game is running.

### What decides the checks

`scope.py` has one rule per path: the editions a change there reaches, whose game it makes stale, which suites
cover it and whether a game run is called for. A release of docs and installer changes needs no game build; a
change under `win-x86/` needs no Mac run; a change to the recompiler, the renderer or the tables needs both. A file
no rule knows counts as everything and is listed, so give a new directory its rule in the same change. `--all` is
there for a release you want checked whole.

What a release leaves unrun is said, not hidden: in the commit's "Checked for the release" paragraph, and in
known-issues.md's "Not tested yet" entry when a player could meet it.

### Game runs

Learn & Play's run uses the game's own input script ([Windows testing](../windows/TESTING.md#unattended-runs)) and
needs a test build and a test user folder. The Mac edition has no input script of its own, so its run takes a route
command from you ([Mac testing](../mac/TESTING.md#game-runs)); without one, play it by hand to the highway and
say so in the commit. Either way the pass line is "the game reached the highway and stayed up", and the last
snapshot is what shows it.

### The installer of a release

The installer downloads the source of its own version's tag, so the published installer can only be run after
step 8. To try it before, start it with `RS_SOURCE_URL` set to an archive of the release commit
(`git archive --format=tar.gz --prefix=rs-rearmed-1.13.0/ HEAD`), in a test home
([Mac testing](../mac/TESTING.md#installer-tests)). This is an extra, for a release that changes the installer.

### If publishing stops half way

`publish.sh` creates the release as a draft and publishes it only when the uploaded zip downloads with the
SHA-256 `package.sh` wrote. If it stops after the push, `publish.sh 1.13.0 --yes --resume` goes on from the tag
that is there. A zip rebuilt later has another checksum: publish the one that was checked.

## Rules that hold

- Ask the maintainer before step 8 ([AGENTS.md](../../AGENTS.md)): it is the only step that changes anything remote.
- The seed is never in git; `package.sh` takes it from `RS_LP_SEED` and refuses to build a release's installer
  without it ([Shipping the seed](../../win-x86/unpack/README.md#shipping-the-seed)).
- What gets pushed is `main` as it should read: one commit per piece of work, the repository's identity, UTC, and
  no home paths or machine names. `publish.sh` checks the last three and lists the commits for the first.
