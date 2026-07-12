#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/html-log-norrd-linecount.sh
#
# Regression guard for issue #234: the disk-page graph paging must not count
# status lines whose filesystems NORRDDISKS/RRDDISKS hold out of the RRDs -
# those slots would render as broken images. htmllog mirrors the do_disk
# filter when counting; the patterns are compiled once per process, so the
# harness runs once per scenario.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile" 2>/dev/null || true)

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-norrd-lc.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/html-log-norrd-linecount-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

common_env() {
	env XYMONHOME="$work" CGIBINURL="/xymon-cgi" RRDWIDTH=576 RRDHEIGHT=120 \
	    XYMONSKIN="/xymon/gifs" XYMONWEB="/xymon" IMAGEFILETYPE="gif" \
	    TEST2RRD="disk" GRAPHS="la,disk" \
	    INFOCOLUMN="info" TRENDSCOLUMN="trends" ACKUNTILMSG="until %H:%M" "$@"
}

run_case() {  # run_case <mode> [ENV=val...]
	mode=$1; shift
	common_env "$@" "$work/harness" "$mode" >"$work/out.log" 2>"$work/err.log" \
		|| fail "scenario '$mode' failed: $(cat "$work/err.log")"
}

run_case none
run_case excl NORRDDISKS='^/poudriere'
run_case incl RRDDISKS='^/zdata/fs'
run_case both RRDDISKS='^/' NORRDDISKS='^/zdata/fs1$'

pass "disk-page graph paging mirrors the NORRDDISKS/RRDDISKS filters"
