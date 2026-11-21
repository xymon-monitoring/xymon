#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/rpcinfo-many-services.sh
#
# A host whose rpc= list is long enough to fill the rpcinfo status buffer must
# get its status, and xymonnet must not overflow the buffer doing so.
#
# send_rpcinfo_results() appends one line per wanted RPC service to a buffer of
# MSGBUFSIZE bytes, bounded by what was left of it -- with no room for the
# terminator, so once the lines filled it the NUL landed one byte past the end.
# Built with _FORTIFY_SOURCE, the default with gcc -O2 on most Linux systems,
# that aborts xymonnet outright, and every network test of every host is lost
# with it; without it the byte is written silently, which Valgrind reports.
#
# rpcinfo is replaced through RPCINFO by a script that answers with a portmap
# table, so no portmapper is needed; the services asked for are names no
# rpc database has, each reported on a line of its own.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/home/tmp"

cat >"$work/rpcinfo" <<'SH'
#!/bin/sh
echo "   program vers proto   port  service"
echo "    100000    2   tcp    111  portmapper"
exit 0
SH
chmod +x "$work/rpcinfo"

# 100 lines of about 55 bytes: well past the 4096 of MSGBUFSIZE.
svcs=
for i in $(seq 1 100); do svcs="$svcs,nosuchservice${i}xxxxxxxxxxxxxxxxx"; done
printf '127.0.0.1\trpchost\t# testip noping rpc=%s\n' "${svcs#,}" >"$work/home/etc/hosts.cfg"

run() {
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe RPCINFO="$work/rpcinfo" \
		"$@" --no-update --timeout=3
}

rc=0
out=$(run "$XYMONNET" 2>&1) || rc=$?
assert_not_contains "buffer overflow detected" "$out" "xymonnet overflowed the rpcinfo status buffer"
[ "$rc" -eq 0 ] || fail "xymonnet exited $rc on a host with $(printf '%s' "$svcs" | tr ',' '\n' | grep -c .) RPC services: $(printf '%s\n' "$out" | tail -5)"
assert_contains "&red Unknown RPC service nosuchservice1xxxxxxxxxxxxxxxxx" "$out" \
	"the rpcinfo status does not list the services asked for"

claim="a host with more RPC services than the rpcinfo status buffer holds gets its status, and the buffer is not overflowed"

# Where _FORTIFY_SOURCE is absent the overflow is a silent byte: Valgrind sees
# it. Only an invalid access counts here: build/valgrind.sh also counts leaks,
# and xymonnet leaves some at exit that have nothing to do with this buffer.
vrc=0
VALGRIND_LOG="$work/vg.log" VALGRIND_OPTS="--child-silent-after-fork=yes" \
	run "$root/build/valgrind.sh" "$XYMONNET" >"$work/vg.out" 2>"$work/vg.err" || vrc=$?
if [ "$vrc" -eq 77 ]; then
	pass_partial "$claim" "valgrind is not installed: only a build with _FORTIFY_SOURCE would have caught the overflow"
fi
[ -s "$work/vg.log" ] || fail "xymonnet exited $vrc under Valgrind and left no report: $(sed -n '1,10p' "$work/vg.err")"
if grep -qE 'Invalid (write|read)' "$work/vg.log"; then
	grep -A12 -m1 -E 'Invalid (write|read)' "$work/vg.log" >&2
	fail "Valgrind reports an invalid memory access in xymonnet on a long rpc= list"
fi
pass "$claim"
