#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-combo-sender.sh
#
# xymonproxy splits a "combo" it receives into its statuses, and adds to each
# "Status message received from ADDRESS". Each part's buffer kept room for an
# IPv4 address, 15 characters at most; an IPv6 one is up to 45, so once the
# proxy takes IPv6 clients, a status of the wrong length from a long IPv6
# address would be written past its buffer.
#
# The proxy is built with AddressSanitizer, and the test runs in a network
# namespace of its own (unshare -rn), where a dummy interface holds the
# 39-character 2001:db8:1111:2222:3333:4444:5555:6666. From there it sends the
# proxy a combo of one status of 8125 bytes: the old buffer was 8192 bytes, and
# the part needs 8202. The proxy must take it without an AddressSanitizer
# report. Where the namespace cannot be made (no unshare, no ip, or
# unprivileged user namespaces not allowed) or there is no AddressSanitizer,
# the check is reported as not checked.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

LONG6=2001:db8:1111:2222:3333:4444:5555:6666
claim="xymonproxy splits a combo from a long IPv6 address without writing past the buffer"
# Where the namespace or AddressSanitizer is not to be had, the check cannot
# be made: reported as not checked, not skipped, as the area must run
notchecked() { pass_partial "$claim" "$1"; }

if [ -z "${XYMONPROXY_COMBO_NS:-}" ]; then
	command -v unshare >/dev/null 2>&1 || notchecked "no unshare, to make a network namespace"
	command -v ip >/dev/null 2>&1 || notchecked "no ip, to give the namespace a long IPv6 address"
	unshare -rn true 2>/dev/null || notchecked "unprivileged user namespaces are not allowed here"
	exec unshare -rn env XYMONPROXY_COMBO_NS=1 bash "$0" "$@"
fi

ip link set lo up
ip link add d0 type dummy 2>/dev/null || notchecked "cannot add a dummy interface in the namespace"
ip link set d0 up
ip -6 addr add "$LONG6/128" dev d0 nodad 2>/dev/null || notchecked "cannot give the namespace an IPv6 address"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMONCLIENT common/xymon
work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a libxymontime.a libxymon.a

echo 'int main(void){return 0;}' >"$work/probe.c"
"$CC" -fsanitize=address -o "$work/probe" "$work/probe.c" 2>/dev/null && "$work/probe" 2>/dev/null \
	|| notchecked "cc does not support -fsanitize=address"

pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || pcre_libs=$(xymon_makevar "$ROOT" PCRELIBS)
# shellcheck disable=SC2046,SC2086  # the flag lists are meant to split
"$CC" -g -fsanitize=address $(xymon_cflags "$ROOT") -o "$work/xymonproxy" "$ROOT/xymonproxy/xymonproxy.c" \
	"$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymontime.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| fail "xymonproxy does not build with AddressSanitizer: $(cat "$work/cc.log")"

# The proxy, on the long address; its server a closed port, as the split
# happens before anything is sent
pport=$(free_port)
ASAN_OPTIONS=detect_leaks=0 "$work/xymonproxy" --no-daemon --listen="[$LONG6]:$pport" \
	--server="127.0.0.1:1" >"$work/proxy.log" 2>&1 &
ppid=$!
register_cleanup "stop_process $ppid"
for _ in {1..100}; do
	grep -q 'Sending to Xymon' "$work/proxy.log" 2>/dev/null && break
	kill -0 "$ppid" 2>/dev/null || fail "xymonproxy did not start: $(cat "$work/proxy.log")"
	sleep 0.1
done
grep -q 'Sending to Xymon' "$work/proxy.log" 2>/dev/null || fail "xymonproxy did not come up: $(cat "$work/proxy.log")"

# One status of 8125 bytes in a combo, from the long address
status="status hosta.conn green "
pad=$(( 8125 - ${#status} ))
msg="combo
${status}$(printf '%*s' "$pad" '' | tr ' ' x)"
XYMON_TIMEOUT=5 "$XYMONCLIENT" "[$LONG6]:$pport" "$msg" >/dev/null 2>&1 \
	|| fail "the combo did not reach the proxy on [$LONG6]:$pport: $(cat "$work/proxy.log")"

# The split comes after the proxy has read the whole message; give it time
for _ in {1..30}; do
	grep -q 'AddressSanitizer' "$work/proxy.log" 2>/dev/null && break
	kill -0 "$ppid" 2>/dev/null || break
	sleep 0.1
done
assert_not_contains "AddressSanitizer" "$(cat "$work/proxy.log")" \
	"xymonproxy wrote past a combo part's buffer for a status from a long IPv6 address"
kill -0 "$ppid" 2>/dev/null || fail "xymonproxy stopped on the combo: $(cat "$work/proxy.log")"

pass "$claim"
