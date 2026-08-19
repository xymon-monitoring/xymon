#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-stale-filter.sh
#
# Every generated graph link carries "nostale", which drops an RRD not updated
# for a day. That was read off the file's mtime, which is not the same fact:
# RRDtool writes through mmap, and only a Linux-only utimes() in do_rrd.c kept
# the mtime moving. On a macOS server every mtime froze at creation time while
# readings kept arriving, and a day later every FNPATTERN multi-graph (disk,
# tcp) rendered empty.
#
# The two cases are that divergence, deterministic on any platform: an RRD
# written now whose file looks ancient must be graphed, one last written three
# days ago whose file is new must not. Both fail on the old code.
#
# Drives the real CGI with --debug, like showgraph-single-service.sh, and reads
# the RRD names out of the rrd_graph argument dump.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"

[ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -f "$ROOT/web/showgraph.cgi" ] || skip "tree built without RRD support (no showgraph.cgi)"
require_bin XYMOND_RRD xymond/xymond_rrd

rrddef=$(sed -n 's/^RRDDEF *= *//p' "$ROOT/Makefile")
# Where <rrd.h> lives, as configure found it: /usr/include on Linux, but
# /usr/local/include or /opt/homebrew/include on the BSDs and macOS, and the
# product Makefiles pass it for every RRD-using object.
rrdinc=$(sed -n 's/^RRDINCDIR *= *//p' "$ROOT/Makefile")
rrdlibs=$(sed -n 's/^RRDLIBS *= *//p' "$ROOT/Makefile")
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"

# PCRELIBS: explicit env override first, else the configured tree's own
# setting (which carries any -L for a nonstandard install). The tree is
# required to be built by now, so the Makefile always carries one.
pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")

work=$(mktempdir)

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags $rrddef $rrdinc -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $rrdlibs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

"$CC" $harness_cflags $rrddef $rrdinc -o "$work/mkrrd" \
	"$(dirname "$0")/showgraph-stale-filter-harness.c" $rrdlibs $harness_ldflags 2>"$work/cc2.log" \
	|| { cat "$work/cc2.log" >&2; fail "the RRD-building harness does not compile"; }

mkdir -p "$work/rrd" "$work/tmp"
now=$(date +%s)
rrds="$work/rrd/testhost"

# The fresh RRD comes from production: a disk status message through
# xymond_rrd. The stale one cannot -- only creation ("-b") can be backdated,
# which is what the harness beside this file does.
{
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$now" "$((now+1800))" "$now" "$now"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /fresh\n'
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null

fresh="$rrds/disk,fresh.rrd"
old="$rrds/disk,old.rrd"
# The || true keeps set -e from killing the group at ls (no directory at
# all) before fail can say what happened.
[ -f "$fresh" ] || { ls -l "$rrds" >&2 || true; fail "xymond_rrd created no RRD from a disk status message"; }
"$work/mkrrd" "$old" "$((now - 3*86400))" || fail "cannot build the stale RRD"

# The divergence, set up so that neither case can be answered from the mtime:
#
#   fresh.rrd  written now,          file backdated to 2020  -> must be graphed
#   old.rrd    written 3 days ago,   file created just now   -> must not be
#
# The second needs no help: an RRD built from an old reading is a new file.
touch -t 202001010000 "$fresh"

# The whole answer is kept, and only the DEF lines are handed back: the image
# follows the argument dump on the same stream, and its NULs have no business
# in a shell variable. What was kept is asserted on separately below.
render() {	# render <extra query args>
	REQUEST_METHOD=GET \
	QUERY_STRING="host=testhost&service=disk&graph=hourly&action=view$1" \
	XYMONHOME="$work" TEST2RRD="disk=disk" \
		"$work/showgraph" --debug --config="$ROOT/xymond/etcfiles/graphs.cfg.DIST" \
		--rrddir="$rrds" >"$work/answer" 2>"$work/errors" || true
	# -a and LC_ALL=C: the answer carries the PNG's bytes, and a grep that
	# goes binary-mode returns nothing instead of the DEF lines.
	LC_ALL=C grep -a 'DEF:' "$work/answer" || true
}

# Without the flag both are drawn: whatever the test observes below is the
# staleness filter, and not the file selection that precedes it.
out=$(render "")
assert_contains "disk,fresh.rrd" "$out" "an unfiltered graph must carry the freshly written RRD"
assert_contains "disk,old.rrd"   "$out" "an unfiltered graph must carry the stale RRD too"

out=$(render "&nostale")
assert_contains "disk,fresh.rrd" "$out" \
	"an RRD written this second was dropped because its file looks old -- this is the macOS blank-graph failure, where mmap leaves the mtime behind"
assert_not_contains "disk,old.rrd" "$out" \
	"an RRD last written three days ago was graphed because its file is new -- a decommissioned host keeps its line on every multi-display"

# The graph is drawn after the dump the assertions above read, so require the
# image: it is the only sign rrd_graph() ran. -a/LC_ALL=C because the answer
# carries PNG bytes; no producer pipeline (grep's early exit SIGPIPEs it under
# pipefail, seen on FreeBSD).
LC_ALL=C grep -aq 'IHDR' "$work/answer" \
	|| { sed -n '1,12p' "$work/answer" >&2
	     fail "the request selected its RRD and then produced no image -- rrd_graph() failed after the staleness probe"; }

# When every matching file is dropped as stale, the empty answer must say so
# (#377) -- not silence, and not the FNPATTERN blame that misdirects to
# graphs.cfg.
mv "$fresh" "$work/fresh.aside"
out=$(render "&nostale")
assert_not_contains "disk," "$out" "with every matching RRD stale, nothing may be graphed"
errs=$(cat "$work/errors")
assert_contains "no RRD for service 'disk' - 1 matching file(s) dropped by the nostale filter" "$errs" \
	"an all-stale result must name the service, the count and the staleness drops in the log"
assert_not_contains "FNPATTERN" "$errs" \
	"an all-stale result must not blame the FNPATTERN capture -- that red herring is issue #377"
mv "$work/fresh.aside" "$fresh"

pass "the nostale filter reads when the RRD was last written, not when its file was touched"
