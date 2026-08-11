#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd

ROOT=$(find_root)
require_c_buildenv "$ROOT"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

buildflags_output=$(make -s -C "$ROOT" -f Makefile -f - inode-test-flags <<'EOF'
.PHONY: inode-test-flags
inode-test-flags:
	@printf '%s\n' 'ldflags=$(LDFLAGS)' 'rpathopt=$(RPATHOPT)' \
		'rrddef=$(RRDDEF)' 'rrdincdir=$(RRDINCDIR)' 'rrdlibs=$(RRDLIBS)'
EOF
) || fail "cannot read configured RRD build flags"
mapfile -t buildflags <<< "$buildflags_output"
[ "${#buildflags[@]}" -eq 5 ] || fail "configured RRD build flags are incomplete"
ldflags=${buildflags[0]#ldflags=}
rpathopt=${buildflags[1]#rpathopt=}
rrddef=${buildflags[2]#rrddef=}
rrdincdir=${buildflags[3]#rrdincdir=}
rrdlibs=${buildflags[4]#rrdlibs=}
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"

# Configured flags are deliberate word-split lists.
# shellcheck disable=SC2086
"$CC" $ldflags -iquote "$ROOT/include" $rrddef $rrdincdir \
	-o "$work/rrd-lastupdate" $rpathopt \
	"$ROOT/tests/rrd/inode-unix-columns-harness.c" $rrdlibs \
	2>"$work/cc.log" || {
	cat "$work/cc.log" >&2
	fail "librrd last-update harness does not compile"
}

timestamp=$(date +%s)
message="@@status|$timestamp|127.0.0.1||unix.test|inode|$((timestamp + 1800))|green||||||||||unix|
status unix.test.inode green $timestamp - Filesystems ok
Filesystem itotal iused ifree %iused Mounted on
/dev/ld0a 259070 41020 218050 15% /
@@
"

printf '%s' "$message" | env \
	XYMONHOME="$work/home" \
	XYMONVAR="$work" \
	XYMONTMP="$work/tmp" \
	XYMONRRDS="$work/rrd" \
	HOSTSCFG="$work/hosts.cfg" \
	TEST2RRD="inode" \
	GRAPHS="inode" \
	"$XYMOND_RRD" --no-cache --rrddir="$work/rrd" \
	>"$work/xymond_rrd.log" 2>&1

rrd="$work/rrd/unix.test/inode,root.rrd"
assert_file_exists "$rrd" "UNIX-column root inode RRD"

lastupdate=$("$work/rrd-lastupdate" "$rrd")
assert_match 'pct[[:space:]]+used' "$lastupdate" \
	"inode RRD defines percentage and used-inode data sources"
assert_match "${timestamp}:[[:space:]]+15([.]0+)?[[:space:]]+41020([.]0+)?" "$lastupdate" \
	"UNIX columns store percentage and used-inode values"

pass "UNIX inode columns reach the shared RRD pipeline"
