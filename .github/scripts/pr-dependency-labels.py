#!/usr/bin/env python3
"""Keep the dependency labels on open pull requests true.

Three labels describe one axis, and between them they say whether a pull
request can be reviewed on its own:

    blocked          something has to land first
    unblocks         something is waiting on it -- both, for the middle of a
                     stack
    deps-none-found  no dependency in either direction

They are computed from two exact sources and nothing else:

  * the base ref -- a pull request whose base is another open pull request's
    head branch is stacked on it, and GitHub says so without anyone writing
    it down;
  * a "Depends-on: #N" line in the body, one per dependency, which is how
    CONTRIBUTING.md asks a dependency on another pull request to be
    declared.

Prose is not read. An earlier version of this script read "Stacked on #N"
and its neighbours anywhere in a body, and on a body saying "#401 and #402
are stacked on #400" it labelled the pull request that wrote the sentence
blocked by #400. Reading prose can be made precise or complete, not both;
a fixed line is both, and that is what lets the labels be removed as well
as added.

A dependency on a pull request that is closed or merged is satisfied, so
it counts for nothing: the next run takes "blocked" off and, if nothing else
is left, puts "deps-none-found" on.

Usage:  pr-dependency-labels.py [--apply]     (default is a dry run)
        pr-dependency-labels.py --self-test   (offline, no GitHub access)
"""

import json
import os
import re
import subprocess
import sys

REPO = os.environ.get("REPO", "xymon-monitoring/xymon")
AXIS = {"blocked", "unblocks", "deps-none-found"}

# One declaration per line, the line holding nothing else. Fenced blocks are
# skipped, so a description can show the syntax without declaring anything.
DECLARED = re.compile(r"^Depends-on:[ \t]*#(\d+)[ \t]*$", re.M)
FENCED = re.compile(r"^(```|~~~).*?^\1", re.S | re.M)


def gh(*args):
    out = subprocess.run(["gh", *args], capture_output=True, text=True, check=True)
    return out.stdout


def open_prs():
    raw = gh("pr", "list", "--repo", REPO, "--state", "open", "--limit", "300",
             "--json", "number,baseRefName,headRefName,body,labels")
    prs = json.loads(raw)
    for p in prs:
        p["labels"] = {l["name"] for l in p["labels"]}
        p["body"] = p.get("body") or ""
    return {p["number"]: p for p in prs}


def edges(prs):
    """(dependent, blocker) pairs, both ends open."""
    by_head = {p["headRefName"]: n for n, p in prs.items()}
    found = set()
    for n, p in prs.items():
        base = p["baseRefName"]
        if base in by_head and by_head[base] != n:
            found.add((n, by_head[base]))
        for m in DECLARED.findall(FENCED.sub("", p["body"].replace("\r\n", "\n"))):
            m = int(m)
            if m in prs and m != n:
                found.add((n, m))
    return found


def reconcile(prs, found):
    """What to add and remove, per pull request.

    Both sources are exact, so the labels are set to what they say: an edge
    gives "blocked" to the dependent and "unblocks" to the dependency, a pull
    request with neither gets "deps-none-found", and any label of the axis
    the sources do not support is removed.
    """
    blocked = {d for d, _ in found}
    unblocks = {b for _, b in found}
    out = {}
    for n, p in prs.items():
        want = set()
        if n in blocked:
            want.add("blocked")
        if n in unblocks:
            want.add("unblocks")
        if not want:
            want.add("deps-none-found")
        have = p["labels"] & AXIS
        out[n] = (want - have, have - want)
    return out


def self_test():
    """The edge and label rules on fixed pull requests, without GitHub."""
    def pr(base, head, body="", labels=()):
        return {"baseRefName": base, "headRefName": head, "body": body,
                "labels": set(labels)}

    prs = {
        # 2 is stacked on 1 by its base ref, 3 declares 2: a stack of three.
        1: pr("main", "a", labels={"deps-none-found"}),
        2: pr("a", "b"),
        3: pr("main", "c", "Depends-on: #2\r\n"),
        # Prose, a line with more on it, an inline mention and a fenced example
        # declare nothing.
        4: pr("main", "d", "#401 and #402 are stacked on #4\n"
                           "as #6 says, Depends-on: #1\n"
                           "Depends-on: #3 once it lands\n"
                           "see `Depends-on: #1`\n```\nDepends-on: #1\n```\n",
              labels={"blocked"}),
        # A declaration of a pull request that is not open counts for nothing.
        5: pr("main", "e", "Depends-on: #999", labels={"blocked"}),
        # Several declarations, one per line.
        6: pr("main", "f", "Depends-on: #1\nDepends-on: #5"),
    }
    found = edges(prs)
    expect = {(2, 1), (3, 2), (6, 1), (6, 5)}
    assert found == expect, "edges %r, expected %r" % (sorted(found), sorted(expect))

    plan = reconcile(prs, found)
    want = {
        1: ({"unblocks"}, {"deps-none-found"}),
        2: ({"blocked", "unblocks"}, set()),
        3: ({"blocked"}, set()),
        4: ({"deps-none-found"}, {"blocked"}),
        5: ({"unblocks"}, {"blocked"}),
        6: ({"blocked"}, set()),
    }
    for n, (add, remove) in want.items():
        assert plan[n] == (add, remove), \
            "#%d: plan %r, expected %r" % (n, plan[n], (add, remove))
    print("self-test: %d edges, %d pull requests as expected" % (len(found), len(want)))
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    apply_changes = "--apply" in sys.argv
    prs = open_prs()
    found = edges(prs)
    plan = reconcile(prs, found)

    changes = [(n, sorted(a), sorted(r)) for n, (a, r) in sorted(plan.items()) if a or r]

    print("open pull requests: %d   dependency edges: %d   drifted: %d"
          % (len(prs), len(found), len(changes)))
    for d, b in sorted(found):
        print("  #%-5s blocked by #%s" % (d, b))
    if not changes:
        print("nothing to change")
        return 0

    for n, add, remove in changes:
        print("  #%-5s %s%s" % (n,
              "".join(" +" + a for a in add),
              "".join(" -" + r for r in remove)))
        if apply_changes:
            cmd = ["pr", "edit", str(n), "--repo", REPO]
            for a in add:
                cmd += ["--add-label", a]
            for r in remove:
                cmd += ["--remove-label", r]
            gh(*cmd)

    if not apply_changes:
        print("dry run -- pass --apply to reconcile")
    return 0


if __name__ == "__main__":
    sys.exit(main())
