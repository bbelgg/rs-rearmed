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
| 4. The edits | `tools/release/bump.py 1.13.0` | `VERSION`, the issue template, the plans' marks, `next.md` emptied; the notes and a commit message draft |
| 5. The commit | by hand | `Release 1.13.0: …`, finished from the draft |
| 6. The installer | `RS_LP_SEED=… RS_UPDATE_KEY=… tools/release/package.sh` | the installer with the seed, zipped, with its SHA-256 and its signature |
| 7. Ready? | `tools/release/publish.sh 1.13.0` | every check of the tree, the history, the notes and the zip; sends nothing |
| 8. Publish | `tools/release/publish.sh 1.13.0 --yes` | the tag, the push, a draft release with the zip and its signature, the upload compared by download, then published |

Step 4 edits the tree. Everything else the steps write (logs, `checks.json`, the notes, the commit message draft,
the zip) goes to `build/release` (`$RS_RELEASE_OUT`), which git ignores. Steps 2, 3 and 6 build or run, so they
refuse to start while a game is running.

### What decides the checks

`scope.py` has one rule per path prefix, and the longest prefix that matches a file decides for it: the editions
a change there reaches, whose game it makes stale, which suites cover it and whether a game run is called for. A
release of docs and installer changes needs no game build; a change under `win-x86/` needs no Mac run; a change to
the recompiler, the renderer or the tables needs both. What Learn & Play borrows from the Mac edition's folders
reaches both editions too: the Steam layer's sources (`shims/steam/src`), the shader warm-up
(`tools/play-native/warm-shaders.sh`) and the scripts' environment (`tools/rs-env.sh`). A file no rule knows
counts as everything and is listed, so give a new directory its rule in the same change. `--all` is there for a
release you want checked whole.

What a release leaves unrun is said, not hidden: in the commit's "Checked for the release" paragraph, and in
known-issues.md's "Not tested yet" entry when a player could meet it.

### Game runs

Learn & Play's run uses the game's own input script ([Windows testing](../windows/TESTING.md#unattended-runs)) and
needs a test build and a test user folder. The Mac edition has no input script of its own, so its run takes a route
command from you ([Mac testing](../mac/TESTING.md#game-runs)); without one, play it by hand to the highway and
say so in the commit. Either way the pass line is "the game reached the highway and stayed up" (the Mac's route
command, `tools/perf/run.py --route-only`, fails when the game has ended or left the highway by the end of its
wait, as Learn & Play's run does), and the last snapshot is what shows it.

### The installer of a release

The installer downloads the source of its own version's tag, so the published installer can only be run after
step 8. To try it before, start it with `RS_SOURCE_URL` set to an archive of the release commit
(`git archive --format=tar.gz --prefix=rs-rearmed-1.13.0/ HEAD`), in a test home
([Mac testing](../mac/TESTING.md#installer-tests)). This is an extra, for a release that changes the installer.

### The update signature

An installed installer replaces itself with a newer release's only when that release's zip carries the
maintainer's signature: `package.sh` signs the zip with the private key `RS_UPDATE_KEY` names
(`tools/release/sign.swift`, Ed25519) and writes `RS.Re-ARMed.Installer.app.zip.sig`, and `publish.sh` uploads it
beside the zip and checks both by download. The public half is `updatePublicKey` in the installer's source. A file
the installer downloads is not marked as a download, so macOS asks nothing when the new one opens: the signature
is the check there is. `tools/installer/test_update.sh` runs the whole update on made-up releases with a key of
its own; to see the window do it before a release is public, see
[Mac testing](../mac/TESTING.md#installer-tests).

A release without the `.sig`, or signed with another key, is still a release: the installers in use say so and
open its page. So a lost key costs every user one download by hand: make a new one
(`swift tools/release/sign.swift keygen FILE`), put its public half in `updatePublicKey`, and say in the notes
that this release is fetched from the page. A key someone else may have is replaced the same way, at once; the
installers already out keep trusting the old one, for what GitHub serves them as this project's newest release.

### If publishing stops half way

`publish.sh` creates the release as a draft and publishes it only when the uploaded zip downloads with the
SHA-256 `package.sh` wrote and the uploaded signature verifies on it. If it stops after the push, `publish.sh 1.13.0 --yes --resume` goes on from the tag
that is there. A zip rebuilt later has another checksum: publish the one that was checked.

### On GitHub

Steps 6 to 8 can also be run on GitHub, by the same scripts: the **Release** workflow
(`.github/workflows/release.yml`, Actions → Release → Run workflow, or
`gh workflow run release.yml -f version=1.13.0 -f publish=true`). Steps 1 to 5 stay where the games are: the
release commit is made as above and pushed to `main` first, which is a push to ask about like any other. The
workflow then checks that the head of `main` is that commit, writes the notes
(`bump.py --notes --next-from HEAD^`, since the release commit has emptied `next.md`), builds and signs the
installer and runs `publish.sh`. With **Publish** off it stops where step 7 does, having sent nothing; **Resume**
is `--resume`.

Only the maintainers can run it, and only from `main`:

- GitHub starts a workflow by hand only for an account with write access to the repository.
- The workflow's first job refuses anyone but the names in it (a new maintainer is added there), and any branch
  but `main`.
- The update key is the secret `RS_UPDATE_KEY` of the environment `release`, which gives its secrets only to
  `main` and holds the job until one of its reviewers approves it. A workflow changed on another branch, or by a
  pull request, never sees the key.

The environment is set up once (Settings → Environments → `release`): the two maintainers as required
reviewers, deployment branches limited to `main`, and the secret, which is the one line of the key's file
(`gh secret set RS_UPDATE_KEY --env release < "$RS_UPDATE_KEY"` prints nothing of it). Whoever can change the
workflow on `main` and approve the job can use the key, so that is the same people as the ones who may hold it.

The seed is too large for a secret. The workflow takes it out of the newest published release's installer, after
checking that zip's signature with `updatePublicKey`; so a release with a new seed or a new update key is made by
hand. The zip is built on GitHub's Mac, so its SHA-256 is that run's (the run's summary shows `package.txt`), and
no check or game run is made there: what the release was checked with is still the release commit's to say.

## Rules that hold

- Ask the maintainer before step 8 ([AGENTS.md](../../AGENTS.md)): it is the only step that changes anything remote.
  Starting the Release workflow with Publish on is step 8 too.
- The seed is never in git; `package.sh` takes it from `RS_LP_SEED` and refuses to build a release's installer
  without it ([Shipping the seed](../../win-x86/unpack/README.md#shipping-the-seed)).
- The update key is never in git either, nor in a log or a message: `package.sh` takes its path from
  `RS_UPDATE_KEY` and refuses to go on without it, or with a key other than the one the installer trusts
  ([The update signature](#the-update-signature)). Only a release's zip is signed with it. Its one copy outside
  the maintainer's Mac is the `release` environment's secret ([On GitHub](#on-github)).
- What gets pushed is `main` as it should read: one commit per piece of work, the repository's identity, UTC, and
  no home paths or machine names. `publish.sh` checks the last three and lists the commits for the first.
