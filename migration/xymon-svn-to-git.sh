#!/usr/bin/env bash
set -euo pipefail

WORKDIR="$HOME/xymon-migration"
DUMPFILE="$WORKDIR/xymon.svndump"
GITDIR="$WORKDIR/xymon-git"

mkdir -p "$WORKDIR"

reposurgeon <<EOF
read <$DUMPFILE
prefer git
rebuild $GITDIR
EOF

cd "$GITDIR"
git log -1 --oneline
git tag | wc -l

