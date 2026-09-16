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

Trying every pair is 70 * 69 / 2 = 2415 of them, two merges each. Sharing a
file is what makes
two pull requests worth test-merging, and that is what the overlap test is
for: at 74 open pull requests it leaves 205 pairs, about a minute of work. It
narrows rather than proves -- git can also conflict on a path relationship, a
file against a directory of the same name, and that pair is not caught here.

A pull request that no longer merges onto main by itself is asked about once,
before any pairing. Paired, it would report the same failure against every
pull request it happens to share a file with: one stale branch produced 40 of
the first run's report lines, and none of them were about a pair.

The paths come from the fetched ref, not from the API's file list. The list
stops at 100 files without saying so, and it names only the path a rename
arrived at -- so a pull request that renames a file and one that edits it
would look like they share nothing.

A stack is excluded rather than reported: where one pull request is built on
another, the second already contains the first and merges cleanly by
construction. Only pull requests that are independent AND collide are
interesting.

Rebasing a stack keeps the changes and loses the commit ids, and then
--is-ancestor no longer sees it. #400, #401 and #402 are one series in that
state: each carries the ones before it under new ids, and merged as a pair
they conflict over work that is simply arriving twice. So the changes are
compared by content as well, and a pull request whose changes are all
contained in another's is a stack whatever its commit ids say.

Usage:  pr-conflicts.py [--json]
"""

import json
import os
import re
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
    # errors="replace": a diff or a path is not always UTF-8, and an
    # undecodable byte must not end the run. Replacement is deterministic, so
    # what is compared downstream stays comparable.
    return subprocess.run(["git", *args], capture_output=True, text=True,
                          errors="replace", **kw)


def need(r, what):
    """A step with no useful answer if it fails: say so and stop.

    Left to run on, a scratch clone that was never set up answers every pair
    the same way, and the report looks ordinary.
    """
    if r.returncode != 0:
        sys.exit("cannot %s: %s" % (what, r.stderr.strip()))
    return r


def open_prs():
    """Open pull requests by number and title, to a limit of 300. Paths come
    later, from the ref."""
    raw = gh("pr", "list", "--repo", REPO, "--state", "open", "--limit", "300",
             "--json", "number,title,isDraft")
    return {p["number"]: {"title": p["title"], "draft": p["isDraft"]}
            for p in json.loads(raw)}


def label(p, width=64):
    """How a pull request is named in the report.

    A draft is tested like any other -- it conflicts just as hard -- but it is
    said so, because most of what a draft collides with is not settled yet and
    a reader is owed that before spending an afternoon on it.
    """
    mark = "(draft) " if p["draft"] else ""
    return mark + p["title"][:width - len(mark)]


def changed_files(work, base, ref):
    """The paths a pull request touches, read from its ref.

    --no-renames on purpose: a rename has to show both the path it left and
    the path it took, or it hides an overlap with whoever edits the original.
    """
    r = git("-c", "core.quotePath=false", "diff", "--name-only", "--no-renames",
            "%s...%s" % (base, ref), cwd=work)
    if r.returncode != 0:
        return None
    return {p for p in r.stdout.splitlines() if p and not p.startswith(NOISE)}


def merges_onto_base(work, base, ref):
    """Whether a pull request still merges onto BASE on its own."""
    env = {**os.environ, "GIT_AUTHOR_NAME": "ci", "GIT_AUTHOR_EMAIL": "ci@localhost",
           "GIT_COMMITTER_NAME": "ci", "GIT_COMMITTER_EMAIL": "ci@localhost"}
    need(git("checkout", "-q", "--detach", base, cwd=work), "check out " + BASE)
    need(git("reset", "-q", "--hard", base, cwd=work), "reset to " + BASE)
    r = subprocess.run(["git", "merge", "-q", "--no-ff", "-m", "probe", ref],
                       cwd=work, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        git("merge", "--abort", cwd=work)
        return False
    return True


def series(work, base, ref):
    """The changes a pull request carries, by content rather than by commit id.

    Two patch ids match when the change is the same, so a rebased copy of a
    commit is recognised as the commit it was rebased from.

    A pipe rather than a round trip through this process: a diff is bytes, not
    text, and one undecodable byte in it would otherwise end the run. Only the
    ids come back here, and those are hex. An empty set on failure is the safe
    way to be wrong, since the caller then declines to call it a stack.
    """
    log = subprocess.Popen(["git", "log", "-p", "--no-color",
                            "%s..%s" % (base, ref)], cwd=work,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    ids = subprocess.run(["git", "patch-id", "--stable"], cwd=work,
                         stdin=log.stdout, capture_output=True, text=True)
    log.stdout.close()
    if log.wait() != 0 or ids.returncode != 0:
        return frozenset()
    return frozenset(l.split()[0] for l in ids.stdout.splitlines() if l.strip())


def referenced(text):
    """The pull request numbers a text points at, "#123" or a pull request URL.

    Matched as numbers rather than as substrings, so #40 is not found inside
    #406. The "#" has to start something: not C#501, not the &#8212; of an
    HTML entity. A false mark is the direction that costs -- it makes a
    collision look like old news -- so the doubtful cases are left unmarked.
    """
    return ({int(n) for n in re.findall(r"(?<![\w&])#(\d+)\b", text)} |
            {int(n) for n in re.findall(r"/pull/(\d+)\b", text)})


def mentions(nums):
    """What each pull request has written down about the others.

    A description or a comment, not a commit subject: an acknowledgement is
    someone writing the number where it is read on the page. It says a person
    saw the collision, never that anyone acted on it, so the report marks a
    pair and still prints it.
    """
    seen = {}
    for n in nums:
        try:
            d = json.loads(gh("pr", "view", str(n), "--repo", REPO,
                              "--json", "body,comments"))
        except (subprocess.CalledProcessError, ValueError):
            seen[n] = set()
            continue
        text = "\n".join([d.get("body") or ""] +
                         [(c.get("body") or "") for c in d.get("comments") or []])
        seen[n] = referenced(text) - {n}
    return seen


def group_series(prs):
    """Pull requests that are one piece of work at different stages.

    By content, not by commit id, for the same reason the pair loop is: a
    rebase keeps the changes and renames them. Two belong together when one's
    changes contain the other's, so #400, #401 and #402 come out as one group.
    """
    parent = {n: n for n in prs}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    nums = sorted(prs)
    for i, a in enumerate(nums):
        for b in nums[i + 1:]:
            sa, sb = prs[a]["series"], prs[b]["series"]
            if sa and sb and (sa <= sb or sb <= sa):
                parent[find(a)] = find(b)
    whole = {}
    for n in nums:
        whole.setdefault(find(n), []).append(n)
    # Containment is not transitive: #100 and #300 can both contain #200
    # without either containing the other, and those two are forks of one
    # base, not stages of one series. A group that is not a chain is broken
    # up, so a rolled-up line never claims more than was measured.
    out = {}
    for members in whole.values():
        ordered = sorted(members, key=lambda n: len(prs[n]["series"]))
        chain = all(prs[ordered[i]]["series"] <= prs[ordered[i + 1]]["series"]
                    for i in range(len(ordered) - 1))
        for n in members:
            out[n] = members if chain else [n]
    return out


def collapse(conflicts, group):
    """One line per problem rather than one per pair.

    Every step of a series usually carries the step before it, so a pull
    request that clashes with the first clashes with all of them: #172 against
    #400, #401 and #402 is one conflict printed three times.

    Usually, not always -- a later step can move or undo the hunk that
    conflicts while still carrying the earlier patch id. So the roll-up is not
    taken on trust: a line is printed only when every combination it names was
    measured to conflict, and on the identical paths. Anything short of that
    prints as the pairs that were actually measured, because a line naming a
    pair that merges cleanly is worse than three lines that are true.
    """
    rolled = {}
    for c in conflicts:
        ga, gb = min(group[c["a"]]), min(group[c["b"]])
        # Keyed on the pair of groups, not on which endpoint numbered lower in
        # this particular pair, or the same two series print twice.
        left, right = (c["a"], c["b"]) if ga <= gb else (c["b"], c["a"])
        key = (min(ga, gb), max(ga, gb), tuple(c["files"]))
        r = rolled.setdefault(key, {"a": set(), "b": set(), "files": c["files"],
                                    "edges": set(), "titles": {}})
        r["a"].add(left)
        r["b"].add(right)
        r["edges"].add((left, right))
        r["titles"][c["a"]] = c["a_title"]
        r["titles"][c["b"]] = c["b_title"]

    def line(x, y, v):
        # Keyed by group, printed by number: the group with the lower minimum
        # is not always the side a reader expects to read first.
        if x[0] > y[0]:
            x, y = y, x
        return dict(v, a=x, b=y)

    out = []
    for _, v in sorted(rolled.items()):
        x, y = sorted(v["a"]), sorted(v["b"])
        if len(v["edges"]) == len(x) * len(y):
            out.append(line(x, y, v))
        else:
            # Not every combination conflicts. Say only what was measured.
            for left, right in sorted(v["edges"]):
                out.append(line([left], [right], v))
    return out


def side(nums):
    if len(nums) == 1:
        return "#%d" % nums[0]
    return "[%s]" % " ".join("#%d" % n for n in nums)


def noted(c, seen):
    """Which of a line's pull requests already names one on the other side."""
    said = set()
    for x in c["a"]:
        for y in c["b"]:
            if y in seen.get(x, ()):
                said.add(x)
            if x in seen.get(y, ()):
                said.add(y)
    return sorted(said)


def show(heading, stale, conflicts, group, seen=None):
    """One half of the report: what is stale, then what cannot both merge."""
    seen = seen or {}
    print(heading)
    print()
    if stale:
        print("  do not merge onto %s on their own, so not paired:\n" % BASE)
        for t in stale:
            print("    #%-5s %s" % (t["pr"], t["title"]))
        print()
    if not conflicts:
        print("  no pair of independent pull requests conflicts")
        print()
        return
    rolled = collapse(conflicts, group)
    marks = [noted(c, seen) for c in rolled]
    already = sum(1 for m in marks if m)
    print("  pairs that cannot both be merged as they stand "
          "(%d, in %d place%s%s):\n"
          % (len(conflicts), len(rolled), "" if len(rolled) == 1 else "s",
             ", %d already written down" % already if already else ""))
    for c, mark in zip(rolled, marks):
        print("    %s + %s  conflict in %s%s"
              % (side(c["a"]), side(c["b"]), named(c["files"]),
                 "   (noted in %s)" % " ".join("#%d" % n for n in mark)
                 if mark else ""))
        for n in c["a"] + c["b"]:
            print("        #%-5s %s" % (n, c["titles"][n]))
        if seen and not mark:
            # The line the report would like to stop printing as news. One
            # comment naming the other side is what marks it next week, and
            # the authors get told something they can act on either way.
            print('        to note it, in #%d: "conflicts with %s in %s"'
                  % (c["a"][0], " ".join("#%d" % n for n in c["b"]),
                     named(c["files"])))
        print()


def candidates(prs):
    """Pairs sharing at least one file. Anything else is not worth a merge."""
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

    A merge can fail without leaving a conflicted path at all -- a file in the
    way, a ref that is not there. The list is never empty on failure, because
    an empty one reads as "merged cleanly" to the caller and the pair would
    disappear from the report.
    """
    env = {**os.environ, "GIT_AUTHOR_NAME": "ci", "GIT_AUTHOR_EMAIL": "ci@localhost",
           "GIT_COMMITTER_NAME": "ci", "GIT_COMMITTER_EMAIL": "ci@localhost"}
    need(git("checkout", "-q", "--detach", base, cwd=work), "check out " + BASE)
    need(git("reset", "-q", "--hard", base, cwd=work), "reset to " + BASE)
    for first, ref in ((True, a), (False, b)):
        r = subprocess.run(["git", "merge", "-q", "--no-ff", "-m", "probe", ref],
                           cwd=work, capture_output=True, text=True, env=env)
        if r.returncode != 0:
            files = git("diff", "--name-only", "--diff-filter=U",
                        cwd=work).stdout.splitlines()
            git("merge", "--abort", cwd=work)
            if first:
                return ["(does not merge onto %s at all)" % BASE] + files
            return files or ["(merge failed, no conflicted path named)"]
    return None


def named(files, most=6):
    """The conflicted paths, short enough to read. A pair against a wide
    refactor can name a hundred files, and the count is the useful part."""
    if len(files) <= most:
        return ", ".join(files)
    return "%s and %d more" % (", ".join(files[:most]), len(files) - most)


def main():
    titles = open_prs()

    work = tempfile.mkdtemp(prefix="prconf.")
    need(git("clone", "-q", "--no-checkout", ".", work), "clone the repository")
    need(git("remote", "add", "up", "https://github.com/%s.git" % REPO, cwd=work),
         "add the upstream remote")
    need(git("fetch", "-q", "up", BASE, cwd=work), "fetch " + BASE)
    base = need(git("rev-parse", "up/" + BASE, cwd=work),
                "resolve " + BASE).stdout.strip()

    prs, stale = {}, []
    for n in sorted(titles):
        ref = "refs/pr/%d" % n
        if git("fetch", "-q", "up", "refs/pull/%d/head:%s" % (n, ref),
               cwd=work).returncode != 0:
            print("#%d: head cannot be fetched, left out" % n, file=sys.stderr)
            continue
        files = changed_files(work, base, ref)
        if files is None:
            print("#%d: paths cannot be read from its ref, left out" % n,
                  file=sys.stderr)
            continue
        if not files:
            continue
        if not merges_onto_base(work, base, ref):
            stale.append({"pr": n, "title": label(titles[n]),
                          "draft": titles[n]["draft"]})
            continue
        prs[n] = {"title": label(titles[n]), "files": files,
                  "draft": titles[n]["draft"],
                  "series": series(work, base, ref)}

    pairs = candidates(prs)
    drafts = (sum(1 for n in prs if prs[n]["draft"])
              + sum(1 for t in stale if t["draft"]))
    print("open pull requests with files: %d (%d draft)   not merging onto "
          "%s: %d   pairs sharing a file: %d"
          % (len(prs) + len(stale), drafts, BASE, len(stale), len(pairs)),
          file=sys.stderr)

    conflicts = []
    for a, b, shared in pairs:
        ra, rb = "refs/pr/%d" % a, "refs/pr/%d" % b
        # A stack: one already contains the other, so it merges by construction.
        if git("merge-base", "--is-ancestor", ra, rb, cwd=work).returncode == 0:
            continue
        if git("merge-base", "--is-ancestor", rb, ra, cwd=work).returncode == 0:
            continue
        # The same stack after a rebase: new commit ids, the same changes.
        sa, sb = prs[a]["series"], prs[b]["series"]
        if sa and sb and (sa <= sb or sb <= sa):
            continue
        bad = merges_clean(work, base, ra, rb)
        if bad:
            conflicts.append({"a": a, "b": b, "files": bad, "shared": shared,
                              "draft": prs[a]["draft"] or prs[b]["draft"],
                              "a_title": prs[a]["title"],
                              "b_title": prs[b]["title"]})

    if "--json" in sys.argv:
        print(json.dumps({"stale": stale, "conflicts": conflicts}, indent=2))
        return 0

    group = group_series(prs)
    ready = [c for c in conflicts if not c["draft"]]
    # Only the ready half is asked about: a draft is not where anyone spends
    # an afternoon, and this is one API read per pull request.
    seen = mentions(sorted({n for c in ready for n in (c["a"], c["b"])}))
    show("Pull requests that are ready. These are the ones a merge will hit.",
         [t for t in stale if not t["draft"]], ready, group, seen)
    show("The drafts, and what they add. A draft conflicts just as hard, but "
         "what it\ncollides with is not settled yet.",
         [t for t in stale if t["draft"]],
         [c for c in conflicts if c["draft"]], group)
    return 0


if __name__ == "__main__":
    sys.exit(main())
