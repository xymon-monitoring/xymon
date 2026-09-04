#!/usr/bin/env python3
"""Find open pull requests that cannot both be merged.

A review looks at one pull request against main. Two that each apply cleanly
there can still be incompatible with each other, and nothing in the normal
flow asks: GitHub reports both mergeable, both go green, and the collision
surfaces when the second one is merged.

The pairs that matter are not always obvious from the titles. #458 and #487
both send TLS SNI on TCP probes, by different designs, and conflict in
xymonnet.c; #428 and #432 both change which addresses xymond binds, and
conflict in xymond.c. Neither pair was found by reading -- only by trying.

Trying every pair is 70 * 69 / 2 = 2415 merges. Two pull requests that share
no file cannot conflict, so overlap narrows that to a few dozen, which is a
few minutes of work.

A stack is excluded rather than reported: where one pull request is built on
another, the second already contains the first and merges cleanly by
construction. Only pull requests that are independent AND collide are
interesting.

Usage:  pr-conflicts.py [--json]
"""

import json
import os
import subprocess
import sys
import tempfile

REPO = os.environ.get("REPO", "xymon-monitoring/xymon")
BASE = os.environ.get("BASE_BRANCH", "main")

# Files nearly every pull request touches, which would pair everything with
# everything without predicting a real conflict.
NOISE = ("docs/manpages/",)


def gh(*args):
    return subprocess.run(["gh", *args], capture_output=True, text=True, check=True).stdout


def git(*args, **kw):
    return subprocess.run(["git", *args], capture_output=True, text=True, **kw)


def open_prs():
    raw = gh("pr", "list", "--repo", REPO, "--state", "open", "--limit", "300",
             "--json", "number,title,files,isDraft")
    out = {}
    for p in json.loads(raw):
        files = {f["path"] for f in p["files"]
                 if not f["path"].startswith(NOISE)}
        if files:
            out[p["number"]] = {"title": p["title"], "files": files,
                                "draft": p["isDraft"]}
    return out


def candidates(prs):
    """Pairs sharing at least one file. Anything else cannot conflict."""
    pairs = []
    nums = sorted(prs)
    for i, a in enumerate(nums):
        for b in nums[i + 1:]:
            shared = prs[a]["files"] & prs[b]["files"]
            if shared:
                pairs.append((a, b, sorted(shared)))
    return pairs


def merges_clean(work, base, a, b):
    """Merge a then b onto base. Returns None if clean, else the file list.

    Each merge is committed before the next: git refuses to start a second
    merge while one is in progress, which reads as a conflict with no files
    named and is how the first version of this got its answer wrong.
    """
    env = {**os.environ, "GIT_AUTHOR_NAME": "ci", "GIT_AUTHOR_EMAIL": "ci@localhost",
           "GIT_COMMITTER_NAME": "ci", "GIT_COMMITTER_EMAIL": "ci@localhost"}
    git("checkout", "-q", "--detach", base, cwd=work)
    git("reset", "-q", "--hard", base, cwd=work)
    for ref in (a, b):
        r = subprocess.run(["git", "merge", "-q", "--no-ff", "-m", "probe", ref],
                           cwd=work, capture_output=True, text=True, env=env)
        if r.returncode != 0:
            files = git("diff", "--name-only", "--diff-filter=U", cwd=work).stdout.split()
            git("merge", "--abort", cwd=work)
            if ref is a:
                return ["(does not merge onto %s at all)" % BASE] + files
            return files
    return None


def main():
    prs = open_prs()
    pairs = candidates(prs)
    print("open pull requests with files: %d   pairs sharing a file: %d"
          % (len(prs), len(pairs)), file=sys.stderr)

    work = tempfile.mkdtemp(prefix="prconf.")
    git("clone", "-q", "--no-checkout", ".", work)
    git("remote", "add", "up", "https://github.com/%s.git" % REPO, cwd=work)
    git("fetch", "-q", "up", BASE, cwd=work)
    base = git("rev-parse", "up/" + BASE, cwd=work).stdout.strip()
    for n in prs:
        git("fetch", "-q", "up", "refs/pull/%d/head:refs/pr/%d" % (n, n), cwd=work)

    conflicts = []
    for a, b, shared in pairs:
        ra, rb = "refs/pr/%d" % a, "refs/pr/%d" % b
        # A stack: one already contains the other, so it merges by construction.
        if git("merge-base", "--is-ancestor", ra, rb, cwd=work).returncode == 0:
            continue
        if git("merge-base", "--is-ancestor", rb, ra, cwd=work).returncode == 0:
            continue
        bad = merges_clean(work, base, ra, rb)
        if bad:
            conflicts.append({"a": a, "b": b, "files": bad, "shared": shared,
                              "a_title": prs[a]["title"], "b_title": prs[b]["title"]})

    if "--json" in sys.argv:
        print(json.dumps(conflicts, indent=2))
    elif not conflicts:
        print("no pair of independent pull requests conflicts")
    else:
        print("pairs that cannot both be merged as they stand:\n")
        for c in conflicts:
            print("  #%s + #%s  conflict in %s" % (c["a"], c["b"], ", ".join(c["files"])))
            print("      #%-5s %s" % (c["a"], c["a_title"][:64]))
            print("      #%-5s %s" % (c["b"], c["b_title"][:64]))
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
