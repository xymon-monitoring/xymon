# Releasing Xymon

How to cut a release, what happens at each step, and why it is built this
way. The process is two workflows plus five small manual actions; the
manual actions are the review gates, everything mechanical is automated.

This file lives under `.github/` on purpose: `.gitattributes` excludes
`.git*` from `git archive`, so maintainer documentation never ends up in
the release tarballs users download.

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

`Changes` and `RELEASENOTES` are prepared on the **`Changes`** branch
between releases. The ritual there: merge `main` in, then one catch-up commit
adding entries for the pull requests merged since. Bring the branch up
to date and merge it into `main` before running the prep workflow (step
1 of the checklist below) — the tarball ships whatever the tag contains.

The two files have different jobs.

**`Changes`** is the full list — one line per merged pull request, so
`grep` finds it later. Entries start with a verb and say what the change
does for someone running Xymon, in at most 100 characters; tighten the
wording rather than folding the line, since a folded entry returns half
a sentence to `grep`:

```
* Fix single-service graphs matching unrelated RRD files (#139) (Thanks, Mark Felder)
```

Credit follows the work, as `(Thanks, Name)` — the person's name, not
their login. In order: whoever wrote the change; whoever reported the
problem, if a maintainer wrote the fix; whoever reviewed it, when there
is nobody else to name. A change ported from an external patch set
keeps its origin attribution — `(from J. Cleaver)` for the Terabithia
patches. A cosmetic change with no user-visible behavior gets no entry.

**`RELEASENOTES`** is what an administrator needs to know *before
upgrading*: a default that changed, a setting that stops working, a
dependency that is now required. Prose, not bullets, and no pull-request
numbers — the reader is planning an upgrade, not tracing a commit. Most
changes do not belong here; if it cannot break a working installation,
it needs no entry.

The next version's `Changes from X -> Y` heading sits open with a
placeholder date between releases; the date is filled in when the
release is cut (step 1 below). Neither that nor the RELEASENOTES
version bump happens in feature pull requests.

## Step by step

### 1. Close out the changelog

On the `Changes` branch: check every merged pull request has its entry,
fill the release date into the top `Changes from X -> Y (XX Xxx XXXX)`
heading and the RELEASENOTES version, then merge the branch into
`main`. Nothing later touches these files — `dorelease.sh` regenerates
`md5.dat` and manpage stamps only — so a release cut without this step
ships a changelog missing everything since the previous release, with a
placeholder where the date should be.

### 2. Run the prep workflow

GitHub → Actions → **Pre-tag release prep** → *Run workflow* → enter the
version as `X.Y.Z` (for example `4.3.30`).

What it does: recreates the branch `release/X.Y.Z` from the current
`main`, runs `build/dorelease.sh X.Y.Z` — which regenerates
`build/md5.dat` and stamps "Version X.Y.Z + date" into every manpage and
its HTML — commits the result, force-pushes the branch, and opens a pull
request against `main`. It also dispatches the build workflow onto the
branch so the PR gets CI checks, and writes the follow-up commands into
the run summary.

Why a PR instead of committing to `main` directly: a human reviews the
generated diff before it becomes real. The diff should contain only
version stamps and `md5.dat` lines; anything else means the generation
went wrong, and you caught it before releasing.

Reruns are safe: the branch is always recreated from `main`, never
patched on top of an old prep, and an existing `rel-X.Y.Z` tag makes the
workflow refuse to run at all.

### 3. Review and merge the prep PR

Check that the diff is only version stamps and `md5.dat`, and that the
checks are green, then merge.

Note: the checks on this PR come from an explicitly dispatched run, not
from a normal `pull_request` event (pushes made with the workflow's
`GITHUB_TOKEN` never trigger workflows — GitHub's recursion guard). They
will not re-run automatically if someone pushes to the branch by hand;
the branch is workflow-owned, so don't do that — rerun the prep workflow
instead.

### 4. Tag the merge commit

The prep run's summary gives you the exact commands, with the PR URL
already filled in:

```sh
git fetch origin
git tag rel-X.Y.Z "$(gh pr view <PR-URL> --json mergeCommit -q .mergeCommit.oid)"
git push origin rel-X.Y.Z
```

The tag is the deliberate "I approve this release" gate — nothing is
released without it. Tag the PR's merge commit, not `origin/main`:
tagging `main`'s tip would silently include any commits that happened to
land after the merge.

Pushing the tag triggers the release workflow, which builds
`xymon-X.Y.Z.tar.gz` (`git archive` of the tag, `gzip -n -9`), generates
its `.sha256`, and creates a **draft** GitHub release with both attached.

### 5. Publish the draft release

Open the draft release, check the generated notes and the attached
files, publish. This is the last look before it is public.

## Reproducibility

The same tag always produces a byte-identical tarball:

- `git archive` of a tag takes every file's mtime from the tagged
  commit's date — fixed once tagged. Lightweight and annotated tags give
  identical bytes.
- `gzip -n` strips the gzip header's embedded filename and timestamp,
  the one wall-clock-dependent bit.
- Manpage and HTML dates come from `SOURCE_DATE_EPOCH` (the prep
  commit's author date), not from the clock of whoever runs the prep.
- The generator scripts pin `LC_ALL=C`, so sort order and date formats
  do not depend on the caller's locale.

This is what makes the published `.sha256` meaningful: anyone can
rebuild from the tag and verify the download matches.

## Testing locally

The whole pipeline minus the GitHub glue (PR creation, draft release)
can be dry-run in one command, in a throwaway worktree — nothing is
modified or pushed:

```sh
./build/dryrelease.sh 9.9.9
```

Run it twice: the checksum must be identical. Requires `mandoc`
(`apt-get install mandoc`).

## Invariants — don't break these

- **The tag must be named `rel-X.Y.Z`.** The release workflow triggers
  on `rel-*`; any other name releases nothing.
- **`build/md5.dat` keeps the hashes of every version ever shipped.**
  `build/setup-newfiles.c` overwrites an installed web file at upgrade
  time only if its hash matches a known stock version — that is how
  upgrades distinguish "untouched old file" from "locally modified by
  the admin". `generate-md5.sh` folds the old `md5.dat` in on purpose;
  regenerating it from scratch would break upgrade detection.
- **The prep job is pinned to `ubuntu-24.04`**, because the manual page
  converter's output differs between versions and would churn the
  generated HTML.
- **The prep scripts are fail-fast** (`set -euo pipefail` throughout,
  digest checks in `generate-md5.sh`). A half-failed generation aborts
  the workflow before anything is committed or pushed; keep it that way
  when editing them.

## Troubleshooting

- **Prep fails with "Tag rel-X.Y.Z already exists"** — that version was
  already released; you want a new version number.
- **Prep fails with "dorelease.sh produced no changes"** — `main`
  already contains the prep for this version, most likely because the
  prep PR was already merged. Just continue at step 4 (tag it).
- **Rerunning prep after the previous prep PR was closed unmerged** —
  fine; a fresh PR is opened (the workflow only treats *open* PRs as
  existing).
- **Rerunning prep while a prep PR is open** — fine; the branch is
  force-pushed and the open PR updates in place.
- **Two draft releases for the same tag** — re-running the release
  workflow on a tag that already produced a draft creates a *second*
  draft rather than updating the first. GitHub's "release by tag"
  lookup ignores drafts, so `action-gh-release` can't find the existing
  one to reuse. Delete the stale draft and keep the latest; this can't
  happen once a release is published (published releases are found and
  updated in place).
