#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/contest-long-clientcert.sh
#
# xymonnet reports a client certificate whose name is too long for its
# buffer, and does not crash on it (#643). An https URL may name one,
# https://CERT:FILENAME@host/, which xymonnet loads from XYMONHOME/certs; it
# built that path with sprintf() into a PATH_MAX buffer, and a long enough
# name overflowed it as the TLS connection was set up, and glibc aborted
# the program.
#
# Checked with a 5000-character certificate name, against a local TLS peer
# so the connection is made: exit 0, and a "too long" error.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
require_cc

root=$(find_root)
work=$(mktempdir)
"$CC" -o "$work/peer" "$root/tests/xymonnet/tls-sni-peer.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "tls-sni-peer does not compile"; }

mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"

"$work/peer" 10 > "$work/peer.out" 2>/dev/null &
pp=$!
register_cleanup "stop_process $pp"
port=""
for _ in $(seq 1 50); do
	port=$(sed -n '1p' "$work/peer.out" 2>/dev/null || true)
	[ -n "$port" ] && break
	kill -0 "$pp" 2>/dev/null || fail "the TLS peer exited before naming its port"
	sleep 0.1
done
[ -n "$port" ] || fail "the TLS peer never named a port"

long=$(printf '%5000s' '' | tr ' ' A)
printf '127.0.0.1 testhost # testip https://CERT:%s@127.0.0.1:%s/\n' "$long" "$port" > "$work/home/etc/hosts.cfg"

rc=0
(cd "$work" && env XYMONHOME="$work/home" XYMONTMP="$work/tmp" \
	"$XYMONNET" --noping --no-update --timeout=5) >/dev/null 2>"$work/err" || rc=$?
assert_equal "0" "$rc" "xymonnet did not survive a 5000-character client certificate name: $(tail -c 300 "$work/err")"
assert_contains "Cannot load SSL client certificate/key: the name under XYMONHOME/certs is too long" "$(cat "$work/err")" \
	"xymonnet did not report the client certificate name as too long"

pass "xymonnet reports a client certificate name too long for its buffer, and does not crash"
