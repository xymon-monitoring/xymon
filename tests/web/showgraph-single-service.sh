#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-single-service.sh
#
# Regression guard for issue #20: viewing one service out of a bundle
# (service=tcp:conn) must select only that service's RRD file, not every
# file whose name contains the service as a substring. Drives the real
# showgraph CGI with --debug against a fake RRD directory and asserts on
# the rrd_graph argument dump.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

# Needs a configured/built tree (bare-tree CI skips; the post-build suite
# runs it for real) and one built WITH RRD support.
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -f "$ROOT/web/showgraph.cgi" ] || skip "tree built without RRD support (no showgraph.cgi)"

# RRD and SSL build flags as configure detected them (SSLLIBS is empty
# on a tree built without SSL - don't force -lssl on such a link)
rrddef=$(sed -n 's/^RRDDEF *= *//p' "$ROOT/Makefile")
rrdlibs=$(sed -n 's/^RRDLIBS *= *//p' "$ROOT/Makefile")
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-showgraph.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" $rrddef -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $rrdlibs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

# Fake RRD directory; selection is by filename, empty stubs suffice
rrds="$work/rrd/testhost"
mkdir -p "$rrds"
touch "$rrds/tcp.conn.rrd" "$rrds/tcp.proxyconn.rrd" \
	"$rrds/tcp.connfoo.rrd" "$rrds/tcp.conn.extra.rrd" \
	"$rrds/tcp.dns.rrd" "$rrds/tcp.dnsmadeeasy.rrd" \
	"$rrds/tcp.http.a.rrd" "$rrds/tcp.http.b.rrd" \
	"$rrds/nocap.x.rrd" "$rrds/nocap.y.rrd" \
	"$rrds/la.rrd"

# Stock graphs.cfg plus a fall-back definition without a capture group
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"
cat >>"$work/graphs.cfg" <<'EOF'

[nocap]
	FNPATTERN ^nocap.*.rrd
	TITLE No capture group
	YAXIS x
	DEF:p@RRDIDX@=@RRDFN@:sec:AVERAGE
	LINE2:p@RRDIDX@#@COLOR@:x
EOF

# Run one CGI request; rrd_graph fails on the stub files, but the --debug
# argument dump (which carries the selected filenames) comes out first.
render() {
	svc=$1; shift
	{
		env "$@" REQUEST_METHOD=GET \
		QUERY_STRING="host=testhost&service=$svc&graph=hourly&action=view" \
		XYMONHOME="$work" TEST2RRD="dns=tcp" \
			"$work/showgraph" --debug --config="$work/graphs.cfg" \
			--rrddir="$rrds" 2>/dev/null || true
	} | tr -d '\000'
}

# The issue #20 case: bundle fall-back selects on the FNPATTERN capture
out=$(render "tcp:conn")
assert_contains     "=tcp.conn.rrd:"  "$out" "single service from bundle"
assert_not_contains "proxyconn.rrd"   "$out" "single service from bundle"
assert_not_contains "connfoo.rrd"     "$out" "single service from bundle"
assert_not_contains "conn.extra.rrd"  "$out" "single service from bundle"

# Same path via a bare TEST2RRD-mapped service (dns -> [tcp])
out=$(render "dns")
assert_contains     "=tcp.dns.rrd:"   "$out" "bare TEST2RRD service"
assert_not_contains "dnsmadeeasy.rrd" "$out" "bare TEST2RRD service"

# A request resolving to its own gdef (tcp.http -> [http]) keeps the
# substring match: the capture is a subitem (URL), not the service
out=$(render "tcp.http")
assert_contains     "=tcp.http.a.rrd:" "$out" "own-gdef request"
assert_contains     "=tcp.http.b.rrd:" "$out" "own-gdef request"

out=$(render "tcp.http" RRDEXCLUDE='tcp:^tcp\.http\.b')
assert_contains     "=tcp.http.a.rrd:" "$out" "RRDEXCLUDE hides existing graph RRDs"
assert_not_contains "tcp.http.b.rrd"   "$out" "RRDEXCLUDE hides existing graph RRDs"

out=$(render "tcp.http" RRDINCLUDE='http:^tcp\.http\.a')
assert_contains     "=tcp.http.a.rrd:" "$out" "RRDINCLUDE limits existing graph RRDs"
assert_not_contains "tcp.http.b.rrd"   "$out" "RRDINCLUDE limits existing graph RRDs"

# Single-file graph definitions without FNPATTERN must use the same filter.
out=$(render "la")
assert_contains     "=la.rrd:" "$out" "single-file graph"

out=$(render "la" RRDEXCLUDE='la:^la$')
assert_not_contains "=la.rrd:" "$out" "RRDEXCLUDE hides single-file graph RRDs"

out=$(render "la" RRDINCLUDE='la:^tcp$')
assert_not_contains "=la.rrd:" "$out" "RRDINCLUDE limits single-file graph RRDs"

# A fall-back definition without a capture group falls back to the
# substring match instead of keeping the whole bundle
out=$(render "nocap:x")
assert_contains     "=nocap.x.rrd:"   "$out" "no-capture fall-back"
assert_not_contains "nocap.y.rrd"     "$out" "no-capture fall-back"

# An empty service component is rejected up front
out=$(render "tcp:")
assert_contains "Missing graph service name" "$out" "empty service rejected"

pass "showgraph selects single-service RRDs by FNPATTERN component"
