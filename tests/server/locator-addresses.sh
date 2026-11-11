#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/locator-addresses.sh
#
# The locator address is read as written, a name, or an IPv4 short form, and
# one that is refused does not crash the program that asked.
#
# A name ("localhost") and a short form ("127.1", which inet_aton() took) are
# resolved, to the IPv4 address when there is one. And a locator_init() that
# refuses its address leaves no socket: xymond_channel, xymond_filestore and
# svcstatus ignore its return, and their next query handed FD_SET() -1, or,
# without _FORTIFY_SOURCE, waited out a send timeout on it.
#
# Checked: lib/xymonlocator reaches a locator on 127.0.0.1 as 127.1:PORT, as
# 127.1 alone on XYMONDPORT, and as localhost:PORT where /etc/hosts maps it to
# 127.0.0.1; xymond_locator takes --listen=127.1:PORT and localhost:PORT, and
# --listen=127.0.0.1 alone listens on 1984 (where 1984 is free); and a query
# after a refused locator_init() fails at once, without a send attempt.
#
# The client's localhost check shows a name is taken, not that it is
# resolved: the old client sent a name to 0.0.0.0, which Linux delivers to
# the local locator too. What shows the client now resolves names is the
# harness's name that does not resolve: it is refused as an invalid address,
# where the old client sent to 0.0.0.0 and failed only when nothing answered.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin LOCATOR lib/xymonlocator

work=$(mktempdir)
export XYMONTMP="$work"

answers() {	# answers ADDR:PORT -- the locator there replies to a ping
	grep -q "Locator is available" <<<"$("$LOCATOR" "$1" </dev/null 2>/dev/null)"
}

# start LISTEN -- a locator on LISTEN's address and a free port; sets PORT
start() {
	local try p cand
	PORT=
	for try in 1 2 3 4 5 6 7 8 9 10; do
		p=$(( 20000 + (RANDOM % 20000) ))
		"$XYMOND_LOCATOR" --listen="$1:$p" --logfile="$work/locator.log" 2>"$work/start.err" &
		cand=$!
		for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
			kill -0 "$cand" 2>/dev/null || break
			if answers "127.0.0.1:$p"; then PORT=$p; register_cleanup "stop_process $cand"; return 0; fi
			sleep 0.1
		done
		stop_process "$cand"
	done
	fail "xymond_locator did not come up with --listen=$1: $(cat "$work/start.err" "$work/locator.log" 2>/dev/null)"
}

start 127.1
answers "127.1:$PORT" || fail "the client no longer reaches a locator given as 127.1:PORT"
# No port: the client takes XYMONDPORT
grep -q "Locator is available" <<<"$(XYMONDPORT=$PORT "$LOCATOR" 127.1 </dev/null 2>/dev/null)" \
	|| fail "the client did not take XYMONDPORT ($PORT) for an address given without a port"
# "localhost" by name, where it is 127.0.0.1; a host may give it ::1 alone
partial=
if grep -qE '^[[:space:]]*127\.0\.0\.1[[:space:]](.*[[:space:]])?localhost([[:space:]]|$)' /etc/hosts 2>/dev/null; then
	answers "localhost:$PORT" || fail "the client no longer reaches a locator given as localhost:PORT"
	start localhost
else
	partial="localhost by name: /etc/hosts does not map it to 127.0.0.1"
fi

# A refused locator_init(), then a query, as the callers that ignore its return do
require_c_buildenv "$ROOT"
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a
cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include "libxymon.h"

int main(void)
{
	int fails = 0;

	if (locator_init("nosuchhost.invalid:1") == 0) { printf("init accepted a name that does not resolve\n"); fails++; }
	if (locator_query("somehost", ST_RRD, NULL) != NULL) { printf("a query answered without a locator\n"); fails++; }
	if (fails == 0) printf("ok\n");
	return fails;
}
EOF
"$CC" $(xymon_cflags "$ROOT") -o "$work/harness" "$work/harness.c" \
	"$(xymon_lib "$ROOT" libxymoncomm.a)" $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"
out=$("$work/harness" 2>&1) || fail "a query after a refused locator_init() did not just fail: $out"
assert_contains "Invalid locator address nosuchhost.invalid:1" "$out" "a name that does not resolve was not refused as an address"
# Without the guard in call_locator(), the query tries to send on socket -1
assert_not_contains "Send to locator failed" "$out" "a query after a refused locator_init() still tried to send"

# No port: the server takes 1984, unless something on this host already has it
"$XYMOND_LOCATOR" --listen=127.0.0.1 --logfile="$work/default.log" 2>"$work/default.err" &
dpid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	grep -q 'Listening on' "$work/default.log" 2>/dev/null && break
	kill -0 "$dpid" 2>/dev/null || break
	sleep 0.1
done
stop_process "$dpid"
if grep -q 'Address already in use' "$work/default.err" 2>/dev/null; then
	partial="${partial:+$partial; }the server's default port: 1984 is taken on this host"
else
	assert_contains "Listening on 127.0.0.1:1984" "$(cat "$work/default.log" "$work/default.err" 2>/dev/null)" \
		"--listen without a port did not listen on 1984"
fi

claim="the locator is reached by name and by IPv4 short form, takes its default ports, and a query without a locator fails at once"
if [ -n "$partial" ]; then pass_partial "$claim" "$partial"; else pass "$claim"; fi
