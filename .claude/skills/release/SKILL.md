---
name: release
description: Make an RS Re-ARMed release (both editions): scope the checks by what changed, run them, bump the version, write the notes and the release commit, package the installer, and publish after the maintainer's go-ahead. Use when asked to release, cut, ship or prepare a version.
---

# Release RS Re-ARMed

The process and its commands are in `docs/release/README.md`; read it first. This skill is the order to do
it in, what to decide yourself, and the one place to stop. Arguments: the new version (`1.13.0`), or nothing,
in which case propose one (a fix release bumps the last number, anything a player can newly do bumps the middle).

Keep it short. The point of this process is that a release takes minutes of attention: run what `scope.py` names
and nothing more, one game run per edition it names, and say what was left unrun instead of running it.

## 1. Preflight (read-only)

- `git status`, `git log --oneline origin/main..HEAD`, `cat VERSION`. On `main`? Anything uncommitted is either
  part of the release (ask if unclear) or stops it.
- `pgrep -x 'Rocksmith2014|rs2014_lp'`: a game run in progress means no builds; do the steps that only read
  and write text, and wait for the rest.
- `tools/release/scope.py`: show its answer. If it lists files with no rule, add the rules now.

## 2. Texts, while the checks run

Start `tools/release/check.py` in the background, then:

- `docs/release/next.md`: compare with `git log v<last>..HEAD`. Every change a player or mod author notices has
  an entry; add the missing ones in the notes' voice (what is now true, then what it was like).
- `docs/release/known-issues.md`: remove what this release fixes, add what it opens, and rewrite the
  "Not tested yet" entry for this release.
- `README.md`'s "Mac and Windows compared" and the Status sections of both `PLAN.md` files, if what an edition
  supports changed. Unreleased entries carry `(not released)`; `bump.py` turns them into `(new in X.Y.Z)`.

## 3. Checks

- `check.py` must end with no FAIL. Read the log of a failure before anything else; a skip is reported, not fixed.
- Game runs, only for the editions `scope.py` names under "Game runs": `tools/release/smoke.sh --lp` and
  `--mac`, one after the other, never during a build. They need a current test build of that edition; build it
  (in the background, it takes a while) only if `scope.py` names the edition under "Game builds" and the test
  build is older than the change. Look at the last snapshot yourself.
- If a run can't be made here (no test build, no route command, no game files), don't work around it: ask
  whether the maintainer plays it by hand or the release goes out saying so, and record the answer in
  known-issues.md and the commit.

## 4. The release commit

- `tools/release/bump.py X.Y.Z --dry-run`, then without. Read `build/release/notes-X.Y.Z.md` through once as a
  player would.
- Finish `build/release/commit-X.Y.Z.txt`: the subject `Release X.Y.Z: <what a player gets>`, the "Since"
  paragraph by area with what each edition got, the "Checked" paragraph completed with the game runs made or not.
- Commit everything of the release as that one commit, as the repository's identity with `TZ=UTC`, ending with
  the attribution line the session gives.

## 5. Package and the stop

- `RS_LP_SEED=<the seed> tools/release/package.sh`. The seed's place is the maintainer's; ask if it isn't in the
  environment or your notes. Never copy it into the tree.
- `tools/release/publish.sh X.Y.Z` (no `--yes`): every line must be `ok`.
- **Stop and ask.** Show: the version, the commits to be pushed, the notes, the checks made and not made, the
  zip's size and SHA-256. Publishing needs the maintainer's yes in this conversation, for this version; an
  earlier yes or a note saying they usually agree is not one.

## 6. Publish

- `tools/release/publish.sh X.Y.Z --yes`. If it stops, read why; `--resume` continues after a push.
- Report the release's URL and the confirmed SHA-256, and what stays open (unrun checks, a run of the published
  installer). Record the same in your notes for the next release.

## Never

- Push, tag or publish without that yes. Push a `backup/*` or `release/*` branch.
- Weaken the network guard or the offline layers to get a check through.
- Put the seed, game files, logs with game content, home paths or machine names into the tree or a commit.
