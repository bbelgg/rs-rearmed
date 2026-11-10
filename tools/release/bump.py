#!/usr/bin/env python3
"""The edits every release makes, and the texts it needs. Nothing is committed, tagged or sent.

  tools/release/bump.py 1.13.0              edit the tree, write the notes and a commit message draft
  tools/release/bump.py 1.13.0 --dry-run    say what would change
  tools/release/bump.py --notes             print the notes as they stand, for the version in VERSION

In the tree:
  VERSION                                 the new version
  .github/ISSUE_TEMPLATE/bug_report.md    its example version
  docs/mac/PLAN.md, docs/windows/PLAN.md  "(after 1.12.0, not released)" becomes "(new in 1.13.0)"
  docs/release/next.md                    emptied, once its lines are in the notes

In OUT ($RS_RELEASE_OUT, else build/release under $RS_BUILD_DIR or the repository):
  notes-1.13.0.md     the release notes: What's new from docs/release/next.md, Known issues from
                      docs/release/known-issues.md, Install from docs/release/install.md
  commit-1.13.0.txt   the release commit's message to finish: the subject's words and the "Since" paragraph are
                      yours, the "Checked for the release" paragraph comes from check.py's checks.json

Before it: bring docs/release/known-issues.md up to date (what this release closes, what it was not tested
with), and README.md's comparison and both PLAN.md files if the release changes what an edition supports.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
PLANS = ("docs/mac/PLAN.md", "docs/windows/PLAN.md")
TEMPLATE = ".github/ISSUE_TEMPLATE/bug_report.md"
NEXT, KNOWN, INSTALL = "docs/release/next.md", "docs/release/known-issues.md", "docs/release/install.md"
NOT_RELEASED = re.compile(r"\((?:after \d+\.\d+\.\d+, )?not released\)")
EXAMPLE = re.compile(r"(<!-- e\.g\. )\d+\.\d+\.\d+(\. Finder)")
COMMENT = re.compile(r"<!--.*?-->\n?", re.S)


def read(root, rel):
    with open(os.path.join(root, rel)) as f:
        return f.read()


def body(text):
    """A notes file without its title line and its comments (the instructions for whoever edits it)."""
    text = COMMENT.sub("", text)
    lines = text.split("\n")
    if lines and lines[0].startswith("# "):
        lines = lines[1:]
    return "\n".join(lines).strip("\n")


def notes(root, version):
    new = body(read(root, NEXT))
    if not re.search(r"^- \S", new, re.M):
        raise SystemExit("error: %s has no entry: a release needs something new to say" % NEXT)
    parts = ["## What's new", new, "## Known issues", body(read(root, KNOWN)),
             "## Install", body(read(root, INSTALL)).replace("{version}", version)]
    return "\n\n".join(parts) + "\n"


def emptied(text):
    """next.md with its title and comments kept and its entries gone."""
    keep = [m.group(0).rstrip("\n") for m in COMMENT.finditer(text)]
    title = text.split("\n", 1)[0]
    return "\n\n".join([title] + keep) + "\n"


def checked(out):
    """The "Checked for the release" paragraph, from check.py's results."""
    path = os.path.join(out, "checks.json")
    if not os.path.exists(path):
        return "Checked for the release: (no checks.json: run tools/release/check.py, or say what ran.)"
    with open(path) as f:
        c = json.load(f)
    by = {k: [r for r in c["results"] if r["status"] == k] for k in ("pass", "skip", "fail")}

    def names(rs):
        return ", ".join(r["what"] + (" (%s)" % r["note"] if r.get("note") else "") for r in rs)
    s = "Checked for the release"
    if c.get("since"):
        s += ", by what changed since %s" % c["since"]
    s += ": " + (names(by["pass"]) or "no suite") + "."
    if by["skip"]:
        s += " Skipped: %s." % names(by["skip"])
    if by["fail"]:
        s += " FAILED: %s." % names(by["fail"])
    s += " Game runs: (say which edition ran to the note highway on this code, or that none did.)"
    return s


def wrap(text, width=79):
    out, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            out.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    return "\n".join(out + [line])


def bump(root, new, out, dry=False):
    """Make the edits under ROOT and write the texts into OUT. Returns the lines that say what changed."""
    old = read(root, "VERSION").split("\n")[0].strip()
    if not VERSION.match(new):
        raise SystemExit("error: %r is not a version (1.13.0)" % new)
    if tuple(map(int, new.split("."))) <= tuple(map(int, old.split("."))):
        raise SystemExit("error: %s is not after %s" % (new, old))
    text = notes(root, new)  # first: it refuses an empty next.md before anything is written
    said, writes = [], {}
    writes["VERSION"] = new + "\n"
    said.append("VERSION: %s -> %s" % (old, new))
    t = read(root, TEMPLATE)
    t2, n = EXAMPLE.subn(r"\g<1>%s\g<2>" % new, t)
    if n != 1:
        raise SystemExit("error: %s: the example version line is not as expected" % TEMPLATE)
    writes[TEMPLATE] = t2
    said.append("%s: the example version" % TEMPLATE)
    for plan in PLANS:
        p = read(root, plan)
        p2, n = NOT_RELEASED.subn("(new in %s)" % new, p)
        if n:
            writes[plan] = p2
        said.append("%s: %d \"not released\" marks become \"new in %s\"" % (plan, n, new))
    writes[NEXT] = emptied(read(root, NEXT))
    said.append("%s: emptied" % NEXT)
    message = "Release %s: (what a player gets, in a line)\n\n%s\n\n%s\n" % (
        new, "Since %s: (the changes, by area, and what the other edition got or still needs.)" % old,
        wrap(checked(out)))
    said.append("%s: notes-%s.md and commit-%s.txt" % (out, new, new))
    if not dry:
        for rel, content in writes.items():
            with open(os.path.join(root, rel), "w") as f:
                f.write(content)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "notes-%s.md" % new), "w") as f:
            f.write(text)
        with open(os.path.join(out, "commit-%s.txt" % new), "w") as f:
            f.write(message)
    return said


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("version", nargs="?")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--notes", action="store_true", help="print the notes and change nothing")
    a = ap.parse_args()
    out = os.environ.get("RS_RELEASE_OUT") or os.path.join(os.environ.get("RS_BUILD_DIR") or
                                                           os.path.join(ROOT, "build"), "release")
    if a.notes:
        sys.stdout.write(notes(ROOT, a.version or read(ROOT, "VERSION").split("\n")[0].strip()))
        return
    if not a.version:
        ap.error("the new version, or --notes")
    for line in bump(ROOT, a.version, out, a.dry_run):
        print(("would: " if a.dry_run else "") + line)


if __name__ == "__main__":
    main()
