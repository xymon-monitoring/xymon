#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-host-path.sh
#
# showgraph finds a host's RRD files under XYMONRRDS/<host>, with the host
# name taken from the request. A host name that is not a plain name, as
# cgi_pathcomponent() defines it, is now refused with an error page, for a
# single host and in a list of hosts; plain names still work. A name longer
# than 255 characters, or a list of more than 1000 hosts, is refused too.
#
# The stub la.rrd in a sibling directory lets the test tell a refused name
# (it never reaches an RRD) from an accepted one.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
require_gnu_make

if [ ! -f "$ROOT/include/config.h" ] || [ ! -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ]; then
	skip "tree not built (run make first; the post-build CI suite covers this)"
fi
[ -f "$ROOT/web/showgraph.cgi" ] || skip "tree built without RRD support (no showgraph.cgi)"

rrddef=$(xymon_makevar "$ROOT" RRDDEF)
rrdinc=$(xymon_makevar "$ROOT" RRDINCDIR)
rrdlibs=$(xymon_makevar "$ROOT" RRDLIBS)
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $harness_cflags $rrddef $rrdinc -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$(xymon_lib "$ROOT" libxymoncomm.a)" \
	$pcre_libs $rrdlibs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

# alpha is a host in the RRD directory; outside/ is a directory next to it
mkdir -p "$work/rrd/alpha" "$work/outside"
: > "$work/outside/la.rrd"
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"

# render HOSTS -- the cpu graph for HOSTS, with the RRD directory from
# XYMONRRDS as on a server (no --rrddir)
render() {
	REQUEST_METHOD=GET \
	QUERY_STRING="host=$1&service=cpu&graph=hourly&action=view" \
	XYMONHOME="$work" XYMONRRDS="$work/rrd" XYMONRUNDIR="$work/run" TEST2RRD="cpu=la" \
		"$work/showgraph" --config="$work/graphs.cfg" 2>/dev/null | tr -d '\000' || true
}

out=$(render "../outside")
assert_contains     "Invalid host name" "$out" "a single host name with a path is refused"
assert_not_contains "mmaping"           "$out" "a single host name with a path reads nothing outside"

out=$(render "alpha,../outside")
assert_contains     "Invalid host name" "$out" "a host name with a path in a list is refused"
assert_not_contains "mmaping"           "$out" "a list with a path reads nothing outside"

out=$(render "a/b")
assert_contains     "Invalid host name" "$out" "a host name with a slash is refused"

out=$(render "alpha")
assert_not_contains "Invalid host name" "$out" "a plain host name is accepted"

out=$(render "alpha,web-1.example.com")
assert_not_contains "Invalid host name" "$out" "a list of plain host names is accepted"

# Bounds: a name of 255 characters and a list of 1000 hosts are accepted,
# one character or one host more is refused
name255=$(printf '%255s' '' | tr ' ' h)
out=$(render "$name255")
assert_not_contains "Invalid host name" "$out" "a host name of 255 characters is accepted"
out=$(render "${name255}h")
assert_contains     "Invalid host name" "$out" "a host name of 256 characters is refused"

list1000=$(seq -f 'h%g' 1 1000 | paste -sd, -)
out=$(render "$list1000")
assert_not_contains "Too many hosts"    "$out" "a list of 1000 hosts is accepted"
out=$(render "$list1000,h1001")
assert_contains     "Too many hosts"    "$out" "a list of 1001 hosts is refused"

pass "showgraph refuses a host name that is not a plain name, and keeps plain ones"
