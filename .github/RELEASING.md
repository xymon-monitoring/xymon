# Releasing Xymon

Two workflows plus five manual actions. The manual actions are the review
gates; everything mechanical is automated.

This file lives under `.github/` on purpose: `.gitattributes` excludes `.git*`
from `git archive`, so maintainer documentation stays out of release tarballs.

## Overview

```
 you: run "Pre-tag release prep"     you: merge PR,                automatic:
 with version=X.Y.Z                  push tag rel-X.Y.Z            on tag push
        │                                  │                            │
        ▼                                  ▼                            ▼
 release/X.Y.Z branch  ──PR──►  main with prep commit  ──tag──►  draft GitHub release
 (md5.dat + manpage                                              (tarball + .sha256)
  version stamps
  regenerated)
```

## The changelog

`Changes` and `RELEASENOTES` are prepared on the **`Changes`** branch, never in
a pull request. The ritual: merge `main` in, add one catch-up commit with an
entry per pull request merged since, then merge `Changes` back into `main`.

**Merge back after every catch-up, not only at release.** Releases are years
apart; main's changelog is what people actually read, and drift left to
accumulate is where entries go missing or land in the wrong section.

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
entry.

**`RELEASENOTES`** — what an administrator needs to know *before upgrading*: a
default that changed, a setting that stops working, a dependency now required.
Prose, no pull-request numbers. Most changes do not belong here; if it cannot
break a working installation, it needs no entry.

The next `Changes from X -> Y` heading sits open with a placeholder date. That
date and the `RELEASENOTES` version bump happen at release, never in a pull
request.

## Step by step

### 1. Close out the changelog

On `Changes`: check every merged pull request has its entry, fill the release
date into the top `Changes from X -> Y (XX Xxx XXXX)` heading and the
`RELEASENOTES` version, then merge into `main`. Nothing later touches these
files — `dorelease.sh` regenerates `md5.dat` and manpage stamps only — so a
release cut without this ships a placeholder where the date should be.

### 2. Run the prep workflow

GitHub → Actions → **Pre-tag release prep** → *Run workflow* → version as
`X.Y.Z`.

It recreates `release/X.Y.Z` from `main`, runs `build/dorelease.sh X.Y.Z`
(regenerating `build/md5.dat` and stamping "Version X.Y.Z + date" into every
manpage and its HTML), commits, force-pushes, and opens a pull request against
`main`. It also dispatches the build workflow so the PR gets CI, and writes the
follow-up commands into the run summary.

A PR rather than a direct commit so a human sees the generated diff first: it
should contain only version stamps and `md5.dat` lines. Anything else means the
generation went wrong.

Reruns are safe — the branch is always recreated from `main`, and an existing
`rel-X.Y.Z` tag makes the workflow refuse outright.

### 3. Review and merge the prep PR

Diff is only version stamps and `md5.dat`, checks green, merge.

The checks come from an explicitly dispatched run, not a `pull_request` event
(pushes made with the workflow's `GITHUB_TOKEN` never trigger workflows). They
will not re-run if someone pushes by hand; the branch is workflow-owned, so
rerun the prep workflow instead.

### 4. Tag the merge commit

```sh
git fetch origin
git tag rel-X.Y.Z "$(gh pr view <PR-URL> --json mergeCommit -q .mergeCommit.oid)"
git push origin rel-X.Y.Z
```

The tag is the "I approve this release" gate — nothing ships without it. Tag
the PR's merge commit, not `origin/main`, whose tip may have moved on.

Pushing it triggers the release workflow: `xymon-X.Y.Z.tar.gz` (`git archive`
of the tag, `gzip -n -9`), its `.sha256`, and a **draft** release with both
attached.

### 5. Publish the draft release

Check the generated notes and attached files, publish.

## Reproducibility

The same tag always produces a byte-identical tarball, which is what makes the
published `.sha256` meaningful:

- `git archive` takes every mtime from the tagged commit's date. Lightweight
  and annotated tags give identical bytes.
- `gzip -n` strips the header's embedded filename and timestamp.
- Manpage and HTML dates come from `SOURCE_DATE_EPOCH` (the prep commit's
  author date), not the clock of whoever runs it.
- The generators pin `LC_ALL=C`, so sort order and date formats do not depend
  on the caller's locale.

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
