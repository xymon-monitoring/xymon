# Releasing Xymon

Two workflows plus five manual actions. The manual actions are the review
gates; everything mechanical is automated.

## Overview

```
 you: run "Pre-tag release prep"     you: merge PR,                automatic:
 with version=X.Y.Z                  push tag rel-X.Y.Z            on tag push
        │                                  │                            │
        ▼                                  ▼                            ▼
 release/X.Y.Z branch  ──PR──►  main with prep commit  ──tag──►  draft GitHub release
 (md5.dat, version.h                                             (tarball + .sha256)
  + manpage version
  stamps regenerated)
```

## The changelog

`Changes` is prepared on the **`Changes`** branch, never in a pull request. The
ritual there: merge `main` in, then one catch-up commit with an entry per pull
request merged since. `RELEASENOTES` is written on `main`, in the pull request
that needs an entry; the `Changes` branch only receives it when `main` is
merged in.

[`CONTRIBUTING.md`](CONTRIBUTING.md) holds the other half of that rule, the part a
contributor needs: put the wording for a `Changes` entry in the pull request
description and it is placed here afterwards; write a `RELEASENOTES` entry in the
pull request itself. How an entry is written is this file's.

**`Changes` merges into `main` only at release** (step 1 below). Between
releases main carries exactly what the last release shipped, so a section
describing a released version cannot move or be reworded underneath it. Only
the open section changes, and it changes on one branch.

**`Changes`** — the full list, one line per merged pull request so `grep` finds
it later. Start with a verb, say what the change does for someone running
Xymon, at most 100 characters. Tighten rather than fold: a folded entry returns
half a sentence to `grep`.

```
* Fix single-service graphs matching unrelated RRD files (#139) (Thanks, Mark Felder)
```

Credit as `(Thanks, Name)` — the person's name, not their login — in order:
whoever wrote the change; whoever reported the problem, if a maintainer wrote
the fix; whoever reviewed it, when there is nobody else to name. A change
ported from an external patch set keeps its origin, `(from J. Cleaver)` for the
Terabithia patches. A cosmetic change with no user-visible behaviour gets no
entry. A test gets one when it pins Xymon's behaviour, as the fix it guards
does; a change to the test framework itself — the runner, the helpers, the
lanes, the portability checks — gets none, like a CI or contributor-document
change: it promises nothing about Xymon, and the people it concerns read the
git log.

**`RELEASENOTES`** — what an administrator needs to know *before upgrading*: a
default that changed, a setting that stops working, a dependency now required.
Prose, no pull-request numbers. Most changes do not belong here; if it cannot
break a working installation, it needs no entry. An entry goes in the open
`Changes for X.Y.Z (not yet released)` section at the top, written in the pull
request that makes the change, so its reviewer reads the two together. The
first pull request after a release that needs an entry opens that section, and
sets the version in the title box above it.

The next `Changes from X -> Y` heading sits open with a placeholder date. That
date, and dropping `(not yet released)` from `RELEASENOTES`, happen at release,
never in a pull request.

## Step by step

### 1. Close out the changelog

On `Changes`: merge `main` in, check every merged pull request has its
`Changes` entry and every one that needed a `RELEASENOTES` entry wrote it, fill
the release date into the top `Changes from X -> Y (XX Xxx XXXX)` heading, drop
`(not yet released)` from the top `RELEASENOTES` heading, then merge into
`main`. Nothing later touches these files — `dorelease.sh` regenerates
`md5.dat` and the version stamps only — so a release cut without this ships a
placeholder where the date should be.

The open sections name the version chosen when they were opened. If the
release gets another number — a 4.4.0 where 4.3.31 was planned — rename it in
the same pass, in three places: the top `Changes from X -> Y` heading, and in
`RELEASENOTES` the title box and the `Changes for X.Y.Z` heading.

### 2. Run the prep workflow

GitHub → Actions → **Pre-tag release prep** → *Run workflow* → version as
`X.Y.Z`.

It recreates `release/X.Y.Z` from `main`, runs `build/dorelease.sh X.Y.Z`
(regenerating `build/md5.dat`, setting `VERSION` in `include/version.h` — what
the binaries report — and stamping "Version X.Y.Z + date" into every manpage
and its HTML), commits, force-pushes, and opens a pull request against
`main`. It also dispatches the build and test workflows so the PR gets CI, and
writes the follow-up commands into the run summary.

A PR rather than a direct commit so a human sees the generated diff first: it
should contain only version stamps, the `include/version.h` line and `md5.dat`
lines. Anything else means the generation went wrong.

Reruns are safe — the branch is always recreated from `main`, and an existing
`rel-X.Y.Z` tag makes the workflow refuse outright.

### 3. Review and merge the prep PR

Diff is only version stamps, `include/version.h` and `md5.dat`, checks green, merge.

If anything has merged into `main` since the prep run started, whatever it
touches, rerun the prep before merging. Its stamps and `md5.dat` were generated
from the older `main`: a web file changed since would ship without its hash in
`md5.dat`, and a manual page added since would ship unstamped.

The checks come from an explicitly dispatched run, not a `pull_request` event
(pushes made with the workflow's `GITHUB_TOKEN` never trigger workflows). A
push made by hand does start CI, as on any branch, but the branch is
workflow-owned and the next prep run recreates it from `main`, discarding that
push — so rerun the prep workflow instead.

A check that failed for a reason outside the branch, such as a flaky lane, needs
no new prep run: re-run it from its run page. It runs again on the same commit,
and its result replaces the earlier one on the PR.

### 4. Tag the merge commit

```sh
git fetch origin
git tag rel-X.Y.Z "$(gh pr view <PR-URL> --json mergeCommit -q .mergeCommit.oid)"
git push origin rel-X.Y.Z
```

The tag is the "I approve this release" gate — nothing ships without it. Tag
the PR's merge commit, not `origin/main`, whose tip may have moved on.

Pushing it triggers the release workflow: `xymon-X.Y.Z.tar.gz` (`git archive`
of the tag with `tar.umask=0022`, `gzip -n -9`), its `.sha256`, and a **draft**
release with both attached.

### 5. Publish the draft release

Check the generated notes and attached files, publish.

## Reproducibility

The same tag always produces a byte-identical tarball, which is what makes the
published `.sha256` meaningful:

- `git archive` takes every mtime from the tagged commit's date. Lightweight
  and annotated tags give identical bytes.
- `gzip -n` strips the header's embedded filename and timestamp.
- `tar.umask=0022` fixes the modes at 644 and 755, rather than git's default
  664 and 775 or whatever the `git` configuration of whoever runs it says.
- Manpage and HTML dates come from `SOURCE_DATE_EPOCH` (the prep commit's
  author date), not the clock of whoever runs it.
- The generators pin `LC_ALL=C`, so sort order and date formats do not depend
  on the caller's locale.

To check a published tarball, rebuild it from the tag and compare:

```sh
git -c tar.umask=0022 archive --format=tar --prefix=xymon-X.Y.Z/ rel-X.Y.Z \
	| gzip -n -9 | sha256sum
```

## Testing locally

The pipeline minus the GitHub glue, in a throwaway worktree — nothing modified
or pushed. Run it twice; the checksum must match. Needs `mandoc`.

```sh
./build/dryrelease.sh 9.9.9
```

## Invariants — don't break these

- **The tag must be `rel-X.Y.Z`.** The release workflow triggers on `rel-*`;
  any other name releases nothing.
- **`build/md5.dat` keeps the hashes of every version ever shipped.**
  `build/setup-newfiles.c` overwrites an installed web file at upgrade only if
  its hash matches a known stock version — that is how upgrades tell "untouched
  old file" from "locally modified". `generate-md5.sh` folds the old file in on
  purpose; regenerating from scratch breaks upgrade detection.
- **The prep job is pinned to `ubuntu-24.04`** — the manpage converter's output
  differs between versions and would churn the generated HTML.
- **The prep scripts are fail-fast** (`set -euo pipefail`, digest checks in
  `generate-md5.sh`). A half-failed generation aborts before anything is
  committed; keep it that way.

## Troubleshooting

- **"Tag rel-X.Y.Z already exists"** — that version shipped; pick a new one.
- **"dorelease.sh produced no changes"** — `main` already has the prep, most
  likely because the prep PR was merged. Continue at step 4.
- **Rerunning prep after the prep PR was closed unmerged** — fine, a fresh PR
  opens (only *open* PRs count as existing).
- **Rerunning prep while a prep PR is open** — fine, the branch is force-pushed
  and the PR updates in place.
- **Two draft releases for one tag** — re-running the release workflow on a tag
  that already produced a draft creates a second one; GitHub's "release by tag"
  lookup ignores drafts, so `action-gh-release` cannot reuse the first. Delete
  the stale draft. Cannot happen once a release is published.
- **"include/version.h at rel-X.Y.Z does not say ..."** — the tag is on a
  commit without the prep PR merged, so the release workflow stopped before
  building anything. Delete the tag (`git push origin :refs/tags/rel-X.Y.Z`,
  then `git tag -d rel-X.Y.Z`) and tag the prep PR's merge commit (step 4).
