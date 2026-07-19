#!/bin/bash
set -euo pipefail

# Local dry-run of the whole release pipeline: prep + tarball + checksum.
# Replays what workflow-prep-release.yaml and workflow-release.yaml do on
# GitHub, inside a throwaway git worktree — your working tree is never
# touched and nothing is pushed anywhere.
#
# Usage: ./build/dryrelease.sh VERSION [OUTDIR]
#
# Run it twice: the printed sha256 must be identical both times. That is
# the reproducibility guarantee, verified on your machine.
#
# Requires man2html (Debian/Ubuntu: apt-get install man2html). Note that
# man2html output differs between versions, so the HTML generated locally
# may differ from CI's — the tarball checksum is only comparable between
# runs on the same machine.

VERSION="${1:-}"
OUTDIR="${2:-/tmp}"
if [ -z "$VERSION" ]; then
	echo "Usage: $0 VERSION [OUTDIR]" >&2
	exit 1
fi

command -v man2html >/dev/null || {
	echo "man2html is required (apt-get install man2html)" >&2
	exit 1
}

REPO=$(git rev-parse --show-toplevel)
W=$(mktemp -d)
cleanup() { cd /; git -C "$REPO" worktree remove --force "$W" 2>/dev/null || true; }
trap cleanup EXIT

git -C "$REPO" worktree add -q "$W" HEAD
cd "$W"

# Same date pinning as the prep workflow. We also pin the dry-run commit's
# own dates so repeated runs give byte-identical tarballs; in the real flow
# the merge commit's date is fixed by nature, so this isn't needed there.
SOURCE_DATE_EPOCH=$(git log -1 --format=%at HEAD)
export SOURCE_DATE_EPOCH

./build/dorelease.sh "$VERSION"
git add -A
GIT_COMMITTER_DATE="@$SOURCE_DATE_EPOCH" git \
	-c user.name=dryrelease -c user.email=dryrelease@localhost \
	commit -q -m "Prep release $VERSION (dry run)" --date="@$SOURCE_DATE_EPOCH"

ASSET="xymon-$VERSION.tar.gz"
git archive --format=tar --prefix="xymon-$VERSION/" HEAD | gzip -n -9 >"$OUTDIR/$ASSET"
(cd "$OUTDIR" && sha256sum "$ASSET" | tee "$ASSET.sha256")

echo "OK: $OUTDIR/$ASSET — run again, the checksum must not change."
