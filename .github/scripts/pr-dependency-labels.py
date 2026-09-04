#!/usr/bin/env python3
"""Keep the dependency labels on open pull requests true.

Three labels describe one axis, and between them they say whether a pull
request can be reviewed on its own:

    blocked          something has to land first
    unblocks         something is waiting on it -- both, for the middle of a
                     stack
    deps-none-found  no dependency was found in either direction

The third name is careful on purpose. "blocked" is an existential claim -- an
edge exists, and one witness proves it. Its opposite is a universal one --
NO edge exists -- which finding nothing cannot establish, because this reads
prose and prose is read imperfectly. So the label says what was found rather
than what is true, and the distance between those two is the reason it is not
called "no-deps".

A pull request carrying none of them has not been looked at, which is only
information while the other three are kept accurate. They go stale on their
own: a base merges, a stack is rebased, a body is edited. This recomputes
them from what the repository actually says and reconciles the difference.

Two sources, both evidence rather than judgement:

  * the base ref -- a pull request whose base is another open pull request's
    head branch is stacked on it, and GitHub says so without anyone writing
    it down;
  * a declaration in the body -- "Stacked on #N", "depends on #N" and the
    like, which is how a stack that shares a base is expressed here.

A declaration naming a pull request that is closed or merged is dropped
rather than recorded: the dependency is satisfied, and keeping it would
leave a pull request marked blocked by something that already landed.

Usage:  pr-dependency-labels.py [--apply]     (default is a dry run)
"""

import json
import os
import re
import subprocess
import sys

REPO = os.environ.get("REPO", "xymon-monitoring/xymon")
AXIS = {"blocked", "unblocks", "deps-none-found"}

# "#" is required. Without it a buffer size in prose ("INT_MIN:LONG_MIN needs
# 34") reads as a dependency on pull request 34.
DECLARED = re.compile(
    r"\b(?:stacked on|depends on|blocked by|requires|on top of|after)\s+#(\d{2,5})\b",
    re.I,
)


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
        for m in DECLARED.findall(p["body"]):
            m = int(m)
            if m in prs and m != n:
                found.add((n, m))
    return found


def reconcile(prs, found):
    """What to add and remove, per pull request.

    Deliberately one-sided. An edge read from a base ref is a fact, but one
    read from a body is a guess at prose: this file has seen "Stacked on #N",
    "#219 -> #172 -> #173", and a bare "Stacked." above a list. A rule that
    removed what it could not explain would strip correct labels somebody set
    by hand -- which is exactly what the first version of this script proposed
    to do to three pull requests.

    So it adds what it can prove and removes only the contradiction:

      * an edge exists      -> add "blocked" / "unblocks", drop "deps-none-found"
      * nothing at all      -> add "deps-none-found", but only where the axis is empty,
                               never over a label a person put there

    "blocked" and "unblocks" are never removed. A dependency that is satisfied
    leaves a label to be dropped by hand, which is a smaller cost than a
    dependency silently unlabelled.
    """
    blocked = {d for d, _ in found}
    unblocks = {b for _, b in found}
    out = {}
    for n, p in prs.items():
        have = p["labels"] & AXIS
        add, remove = set(), set()
        if n in blocked and "blocked" not in have:
            add.add("blocked")
        if n in unblocks and "unblocks" not in have:
            add.add("unblocks")
        if (n in blocked or n in unblocks) and "deps-none-found" in have:
            remove.add("deps-none-found")
        if not (n in blocked or n in unblocks) and not have:
            add.add("deps-none-found")
        out[n] = (add, remove)
    return out


def main():
    apply_changes = "--apply" in sys.argv
    prs = open_prs()
    found = edges(prs)
    plan = reconcile(prs, found)

    changes = [(n, sorted(a), sorted(r)) for n, (a, r) in sorted(plan.items()) if a or r]

    # Where a label exists that no edge explains, say so rather than removing
    # it: either the body wording is one this does not read, or the dependency
    # is over and somebody should take the label off.
    blocked = {d for d, _ in found}
    unblocks = {b for _, b in found}
    unexplained = [n for n, p in sorted(prs.items())
                   if ("blocked" in p["labels"] and n not in blocked)
                   or ("unblocks" in p["labels"] and n not in unblocks)]

    print("open pull requests: %d   dependency edges: %d   drifted: %d"
          % (len(prs), len(found), len(changes)))
    for d, b in sorted(found):
        print("  #%-5s blocked by #%s" % (d, b))
    if unexplained:
        print("labelled but no edge found (left alone -- check the wording or drop the label):")
        for n in unexplained:
            print("  #%s" % n)

    if not changes:
        print("nothing to add")
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
