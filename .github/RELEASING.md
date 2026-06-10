# Releasing Xymon

How to cut a release, what happens at each step, and why it is built this
way. The process is two workflows plus four small manual actions; the
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

## Step by step

### 1. Run the prep workflow

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

### 2. Review and merge the prep PR

Check that the diff is only version stamps and `md5.dat`, and that the
checks are green, then merge.

Note: the checks on this PR come from an explicitly dispatched run, not
from a normal `pull_request` event (pushes made with the workflow's
`GITHUB_TOKEN` never trigger workflows — GitHub's recursion guard). They
will not re-run automatically if someone pushes to the branch by hand;
the branch is workflow-owned, so don't do that — rerun the prep workflow
instead.

### 3. Tag the merge commit

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

### 4. Publish the draft release

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

Run it twice: the checksum must be identical. Requires `man2html`
(`apt-get install man2html`).

## Invariants — don't break these

- **The tag must be named `rel-X.Y.Z`.** The release workflow triggers
  on `rel-*`; any other name releases nothing.
- **`build/md5.dat` keeps the hashes of every version ever shipped.**
  `build/setup-newfiles.c` overwrites an installed web file at upgrade
  time only if its hash matches a known stock version — that is how
  upgrades distinguish "untouched old file" from "locally modified by
  the admin". `generate-md5.sh` folds the old `md5.dat` in on purpose;
  regenerating it from scratch would break upgrade detection.
- **The prep job is pinned to `ubuntu-24.04`**, because `man2html`
  output differs between versions and would churn the generated HTML.
- **The prep scripts are fail-fast** (`set -euo pipefail` throughout,
  digest checks in `generate-md5.sh`). A half-failed generation aborts
  the workflow before anything is committed or pushed; keep it that way
  when editing them.

## Troubleshooting

- **Prep fails with "Tag rel-X.Y.Z already exists"** — that version was
  already released; you want a new version number.
- **Prep fails with "dorelease.sh produced no changes"** — `main`
  already contains the prep for this version, most likely because the
  prep PR was already merged. Just continue at step 3 (tag it).
- **Rerunning prep after the previous prep PR was closed unmerged** —
  fine; a fresh PR is opened (the workflow only treats *open* PRs as
  existing).
- **Rerunning prep while a prep PR is open** — fine; the branch is
  force-pushed and the open PR updates in place.
