#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymon-rundir.sh
#
# xymon_rundir() is how xymond, xymond_history, xymond_rrd, trimhistory,
# showgraph and rrdcachectl build their default pidfile and socket paths:
# XYMONRUNDIR, else the configured log directory XYMONSERVERLOGS, else the
# compiled one, with an empty value counted as unset. Its last step is the one no program-level test
# can reach safely -- it lands in the real compiled log directory -- so this
# runs the function itself. Without that step, XYMONRUNDIR="" together with
# XYMONSERVERLOGS="" gave "", and the programs wrote /xymond.pid and
# /rrdctl.<pid> at the filesystem root.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_c_buildenv "$ROOT"
[ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
logdir=$(sed -n 's/^XYMONLOGDIR *= *//p' "$ROOT/Makefile" 2>/dev/null)
[ -n "$logdir" ] || skip "no XYMONLOGDIR in the top-level Makefile"

work=$(mktempdir); register_cleanup "rm -rf '$work'"

# The library as the build made it, not refreshed with build_xymon_libs: that
# runs make in lib/ without the top-level variables, and a stale environ.o
# would be recompiled with XYMONLOGDIR="" -- the very macro checked below.

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/harness" \
	"$here/xymon-rundir-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

got=$(env -i XYMONRUNDIR="$work/run" XYMONSERVERLOGS="$work/logs" "$work/harness")
assert_equal "$work/run" "$got" "XYMONRUNDIR set"

got=$(env -i XYMONRUNDIR="" XYMONSERVERLOGS="$work/logs" "$work/harness")
assert_equal "$work/logs" "$got" "XYMONRUNDIR empty"

got=$(env -i XYMONSERVERLOGS="$work/logs" "$work/harness")
assert_equal "$work/logs" "$got" "XYMONRUNDIR unset"

got=$(env -i XYMONRUNDIR="" XYMONSERVERLOGS="" "$work/harness")
assert_equal "$logdir" "$got" "XYMONRUNDIR and XYMONSERVERLOGS both empty"

pass "xymon_rundir() falls back from XYMONRUNDIR to XYMONSERVERLOGS to the compiled log directory, empty counting as unset"
