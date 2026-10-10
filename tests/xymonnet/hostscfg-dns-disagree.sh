#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/hostscfg-dns-disagree.sh
#
# xymonnet says when a host's hosts.cfg address and its DNS answer differ.
#
# By default xymonnet tests the address a host's name resolves to, and uses
# the hosts.cfg address only when the name does not resolve. #516 proposes
# testing the hosts.cfg address instead, and ships a warning release first:
# every line whose address DNS contradicts is named in the "Warning output"
# of xymonnet's own status, while the address tested stays the one DNS gave.
#
# "localhost" resolves to 127.0.0.1, where a listener answers the test port.
# Written as 127.0.0.2 it must be named once, and still tested at 127.0.0.1;
# written as 127.0.0.1, or as 0.0.0.0 (resolve it), it must not be named.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"

"$CC" -o "$work/listener" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc.log" \
	|| fail "the listener does not compile: $(cat "$work/cc.log")"
printf 'hello\n' >"$work/reply"
"$work/listener" "$work/reply" >"$work/port" 2>"$work/listener.err" &
register_cleanup "kill $! 2>/dev/null || true"
for _ in $(seq 1 50); do [ -s "$work/port" ] && break; sleep 0.1; done
[ -s "$work/port" ] || fail "the listener did not report its port: $(cat "$work/listener.err")"
printf '[demoport]\n   port %s\n' "$(cat "$work/port")" >"$work/home/etc/protocols.cfg"

# run COLUMN1 -- xymonnet's output for "COLUMN1 localhost # demoport"
run() {
	printf '%s\tlocalhost\t# demoport\n' "$1" >"$work/home/etc/hosts.cfg"
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
		"$XYMONNET" --no-update --noping --report --timeout=5 2>&1 || true
}

out=$(run 127.0.0.2)
n=$(grep -c 'hosts.cfg says' <<<"$out" || true)
[ "$n" -eq 1 ] || fail "a hosts.cfg address that DNS contradicts was named $n times, expected once:
$(grep -E 'Warning output|hosts.cfg says' <<<"$out")"
assert_contains "host localhost: hosts.cfg says 127.0.0.2, DNS says 127.0.0.1; tested 127.0.0.1" "$out" \
	"the warning does not name both addresses and the one tested"
assert_contains "localhost.demoport green" "$out" \
	"the warning release changed what is tested: the listener on 127.0.0.1, which DNS names, was not reached"

out=$(run 127.0.0.1)
assert_not_contains "hosts.cfg says" "$out" "a hosts.cfg address that DNS agrees with was reported"
assert_contains "localhost.demoport green" "$out" "the test failed with an agreeing address"

out=$(run 0.0.0.0)
assert_not_contains "hosts.cfg says" "$out" "0.0.0.0, which asks for resolution, was reported as a disagreement"

pass "xymonnet names a host whose hosts.cfg address DNS contradicts, once, and still tests the address DNS gives"
