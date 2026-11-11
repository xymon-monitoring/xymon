#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-ctlmsg.sh
#
# xymond_rrd takes cache-flush requests ("/host/") on its rrdctl.* datagram
# socket, from showgraph and rrdcachectl. Two properties checked here:
#
# - a message of exactly PATH_MAX bytes is received within the buffer, its
#   terminator inside it;
# - a name that matches nothing cached leaves no rate-limit record: asked
#   twice, it is "nothing cached" twice, never "too soon".
#
# Run against xymond_rrd built from its sources with AddressSanitizer, which
# reports the one-byte write; skipped where the compiler has none.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
asan_usable || skip "the compiler has no AddressSanitizer"
if [ ! -f "$ROOT/include/config.h" ] || [ ! -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ]; then
	skip "tree not built (run make first; the post-build CI suite covers this)"
fi
require_bin XYMOND_RRD xymond/xymond_rrd

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a libxymontime.a

rrddef=$(xymon_makevar "$ROOT" RRDDEF)
rrdinc=$(xymon_makevar "$ROOT" RRDINCDIR)
rrdlibs=$(xymon_makevar "$ROOT" RRDLIBS)
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")

x="$ROOT/xymond"
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" -fsanitize=address -g $cflags $rrddef $rrdinc -iquote "$x" -o "$work/xymond_rrd" \
	"$x/xymond_rrd.c" "$x/xymond_worker.c" "$x/do_rrd.c" "$x/client_config.c" \
	"$(xymon_lib "$ROOT" libxymontime.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" \
	$rrdlibs $pcre_libs $ldflags -lm 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "xymond_rrd does not build with AddressSanitizer"; }
"$CC" -o "$work/sender" "$(dirname "$0")/xymond-rrd-ctlmsg-sender.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the sender does not compile"; }
pathmax=$(getconf PATH_MAX "$work")

mkdir -p "$work/home/etc" "$work/tmp" "$work/run" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"
mkfifo "$work/feed"

ASAN_OPTIONS=detect_leaks=0 \
XYMONHOME="$work/home" XYMONVAR="$work" XYMONTMP="$work/tmp" \
XYMONRUNDIR="$work/run" XYMONRRDS="$work/rrd" HOSTSCFG="!$work/hosts.cfg" \
TEST2RRD="inode" GRAPHS="inode" \
	"$work/xymond_rrd" --rrddir="$work/rrd" --debug \
	<"$work/feed" >"$work/rrd.log" 2>&1 &
worker=$!
register_cleanup "stop_process $worker"
exec 9>"$work/feed"

# status MINUTE -- one inode status for unix.test: an RRD update, which also
# wakes the daemon to read the control messages waiting on its socket.
status() {
	local ts=$(( $(date +%s) + $1 * 60 ))
	printf '@@status|%s|127.0.0.1||unix.test|inode|%s|green||||||||||unix|\n' "$ts" "$((ts + 1800))" >&9
	printf 'status unix.test.inode green %s - Filesystems ok\n' "$ts" >&9
	printf 'Filesystem itotal iused ifree %%%%iused Mounted on\n' >&9
	printf '/dev/ld0a 259070 41020 218050 15%%%% /\n@@\n' >&9
}

status 0
sock=
for _ in $(seq 1 100); do
	for f in "$work"/run/rrdctl.*; do
		[ -S "$f" ] && sock=${f##*/}
	done
	[ -n "$sock" ] && [ -n "$(ls "$work/rrd/unix.test" 2>/dev/null)" ] && break
	sleep 0.1
done
[ -n "$sock" ] || fail "xymond_rrd made no control socket: $(cat "$work/rrd.log")"

"$work/sender" "$work/run/$sock" "/nosuchhost/" "/nosuchhost/" "-$pathmax" \
	|| fail "could not send the control messages"
status 1
status 2
exec 9>&-
wait "$worker" 2>/dev/null || true
log=$(cat "$work/rrd.log")

assert_not_contains "AddressSanitizer" "$log" "a control message of PATH_MAX ($pathmax) bytes stays inside the receive buffer"
assert_contains     "Flush of '/nosuchhost/' skipped, nothing cached" "$log" "a name that matches nothing cached is not flushed"
assert_not_contains "Flush of '/nosuchhost/' skipped, too soon"       "$log" "a name that matches nothing cached keeps no record"

pass "xymond_rrd bounds a control message by its buffer, and keeps no record for a name with nothing cached"
