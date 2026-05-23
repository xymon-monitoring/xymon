#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/configure-no-rrd.sh
#
# Regression guard for xymon-monitoring/xymon#84: './configure --server'
# must exit non-zero when the RRDtool probe fails. Before #84 the
# script would silently set ENABLERRD=n and proceed, producing a
# server build that would then fail at runtime when something tried
# to write an RRD.
#
# Approach: spin up a throwaway git worktree at HEAD, replace
# build/rrd.sh with a stub that forces RRDOK=NO, run ./configure
# --server with stdin closed, and assert rc != 0. The worktree
# isolation means we never mutate the user's working tree even if the
# test is killed mid-run.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

[ -f "$ROOT/configure.server" ] || skip "configure.server missing (CMake-only tree?)"
[ -f "$ROOT/build/rrd.sh" ]     || skip "build/rrd.sh missing"
command -v git >/dev/null 2>&1  || skip "git not found"

TMP=$(mktempdir)
WORKTREE="$TMP/wt"

git -C "$ROOT" worktree add --detach "$WORKTREE" HEAD >/dev/null 2>&1
# Reverse-registration order: this cleanup runs before mktempdir's
# rm -rf, so the worktree metadata under .git/worktrees/ is pruned
# while the dir still exists.
register_cleanup "git -C '$ROOT' worktree remove --force '$WORKTREE' >/dev/null 2>&1; git -C '$ROOT' worktree prune >/dev/null 2>&1 || true"

cat > "$WORKTREE/build/rrd.sh" <<'EOF'
# Stub for tests/buildsystem/configure-no-rrd.sh -- force RRDOK=NO so
# we can assert that configure.server exits cleanly on a failed probe.
echo "Checking for RRDtool ... (mocked: forcing RRDOK=NO)"
RRDOK="NO"
EOF

cd "$WORKTREE"

LOG="$TMP/configure.log"
set +e
./configure --server </dev/null >"$LOG" 2>&1
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
	echo "--- configure log ---" >&2
	cat "$LOG" >&2
	echo "--- end log ---" >&2
	fail "configure --server exited 0 with RRDOK=NO; expected non-zero (regression of #84)"
fi

# Optional belt-and-braces: the post-#84 log mentions the abort reason.
# Don't hard-fail on phrasing changes -- a non-zero exit is the load-
# bearing assertion -- but if the message is present, surface it as
# evidence of the right code path.
if grep -q "Configuration aborted because the RRDtool probe failed" "$LOG"; then
	: ok
fi
