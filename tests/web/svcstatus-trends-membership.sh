#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-trends-membership.sh
#
# Trends-page membership comes from the graph definition: a gdef marked
# TRENDS in graphs.cfg appears on the trends page without a GRAPHS env
# entry, INCLUDE variants inherit it, and unmarked gdefs stay off.
# Drives the real generate_trends() through a small C harness.

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-trendsmbr.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/svcstatus-trends-membership-harness.c" "$ROOT/web/svcstatus-trends.c" \
	"$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# Selection is by filename; empty stubs suffice (no RRD reads happen)
mkdir -p "$work/etc" "$work/rrd/testhost"
touch "$work/rrd/testhost/markermetric.a.rrd" \
	"$work/rrd/testhost/markermetric.b.rrd" \
	"$work/rrd/testhost/incvariant.z.rrd" \
	"$work/rrd/testhost/othermetric.x.rrd"

printf '0.0.0.0 testhost # linux\n' >"$work/etc/hosts.cfg"
cat >"$work/etc/graphs.cfg" <<'GDEFS'
[markermetric]
	FNPATTERN ^markermetric\.(.+)\.rrd
	TITLE Marker metric
	YAXIS v
	MAXINSTANCESPERIMAGE 1
	TRENDS
	DEF:v@RRDIDX@=@RRDFN@:v:AVERAGE
	LINE1:v@RRDIDX@#@COLOR@:@RRDPARAM@

[incvariant]
	INCLUDE markermetric
	FNPATTERN ^incvariant\.(.+)\.rrd
	TITLE Variant

[othermetric]
	FNPATTERN ^othermetric\.(.+)\.rrd
	TITLE Not a member
	YAXIS v
	DEF:v@RRDIDX@=@RRDFN@:v:AVERAGE
	LINE1:v@RRDIDX@#@COLOR@:@RRDPARAM@
GDEFS

XYMONHOME="$work" \
HOSTSCFG="$work/etc/hosts.cfg" \
XYMONRRDS="$work/rrd" \
CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 \
RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" \
XYMONWEB="/xymon" \
IMAGEFILETYPE="gif" \
TEST2RRD="cpu=la" \
GRAPHS="la,disk,tcp" \
	"$work/harness" 2>"$work/stderr.log" || fail "harness assertions failed: $(cat "$work/stderr.log")"

pass "TRENDS gdefs are trends-page members without a GRAPHS entry"
