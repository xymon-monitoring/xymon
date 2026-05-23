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
# Approach: copy the files configure --server actually touches
# (configure, configure.server, build/) into a throwaway temp dir,
# replace build/rrd.sh with a stub that forces RRDOK=NO, run
# ./configure --server with stdin closed, and assert rc != 0. The
# copied tree means we never mutate the user's source tree even if
# the test is killed mid-run.
#
# Deliberately copy-based, not git-worktree-based: tests must work
# from an extracted release tarball or inside a Debian autopkgtest
# chroot, where .git is absent. See tests/README.md.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

[ -f "$ROOT/configure" ]        || skip "configure missing (CMake-only tree?)"
[ -f "$ROOT/configure.server" ] || skip "configure.server missing"
[ -d "$ROOT/build" ]            || skip "build/ missing"

TMP=$(mktempdir)
SRC="$TMP/src"
mkdir -p "$SRC"

# Copy just what configure --server reads: the two configure scripts
# and the build/ directory (build/{rrd,pcre,c-ares,ssl,ldap,...}.sh
# probe scripts and the small Makefile.test-* helpers they invoke).
# Skip everything else -- we don't want to drag in the whole source
# tree or any build artefacts.
cp -p "$ROOT/configure" "$ROOT/configure.server" "$SRC/"
cp -rp "$ROOT/build" "$SRC/build"

# configure.client is referenced by configure (dispatcher) for the
# --help path; copy it if present, but don't require it.
[ -f "$ROOT/configure.client" ] && cp -p "$ROOT/configure.client" "$SRC/"

cat > "$SRC/build/rrd.sh" <<'EOF'
# Stub for tests/buildsystem/configure-no-rrd.sh -- force RRDOK=NO so
# we can assert that configure.server exits cleanly on a failed probe.
echo "Checking for RRDtool ... (mocked: forcing RRDOK=NO)"
RRDOK="NO"
EOF

cd "$SRC"

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
