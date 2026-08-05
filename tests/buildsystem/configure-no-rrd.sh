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
# shellcheck source=tests/lib/assert.sh
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

# configure.server sources build/fping.sh before build/rrd.sh. On hosts
# where fping is present-but-unusable (or USEXYMONPING=n with a failing
# FPING), fping.sh enters an interactive prompt loop; with stdin closed
# that loop spins forever, so the RRD assertion below is never reached.
# Force the non-interactive xymonping path so this test is pinned to
# the RRD probe regardless of host fping state.
export USEXYMONPING=y

LOG="$TMP/configure.log"
set +e
./configure --server </dev/null >"$LOG" 2>&1
rc=$?
set -e

dump_log() {
	echo "--- configure log ---" >&2
	cat "$LOG" >&2
	echo "--- end log ---" >&2
}

if [ "$rc" -eq 0 ]; then
	dump_log
	fail "configure --server exited 0 with RRDOK=NO; expected non-zero (regression of #84)"
fi

# A non-zero exit alone is not sufficient: on a clean runner the rest
# of configure --server has plenty of unrelated ways to fail later
# (missing xymon user, missing libs, ...), so rc != 0 could be
# satisfied even if pre-#84 silent-continue behavior were reintroduced.
# Pin the failure to the RRD abort code path in configure.server.
if ! grep -q "RRDtool probe failed" "$LOG"; then
	# configure.server checks a few prerequisites *before* it reaches the
	# RRD stub: GNU make (it bails with "GNU make is required ...") and a
	# usable PCRE (build/pcre.sh exits with "Missing PCRE include- or
	# library-files ..."). If one of those is absent on this host the run
	# aborts early and never exercises the RRD path -- that's a missing
	# precondition, not a #84 regression, so skip rather than false-fail.
	# (Matches the tarball/autopkgtest portability goal: declare the host
	# dependency and skip when it's not met.)
	if grep -q "GNU make is required" "$LOG"; then
		skip "GNU make unavailable -- configure aborts before the RRD probe"
	fi
	if grep -q "Missing PCRE include- or library-files" "$LOG"; then
		skip "usable PCRE unavailable -- configure aborts before the RRD probe"
	fi
	dump_log
	fail "configure --server exited $rc but not via the RRD abort path (regression of #84)"
fi
