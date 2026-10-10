#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/ipv6-hosts.sh
#
# A host listed in hosts.cfg by an IPv6 address is loaded and tested.
#
# The address of a host line used to be read as four decimal numbers, so a
# line beginning with an IPv6 address was not a host at all. Now either
# family is read (hostscfg_hostline(), shared by every program that loads
# hosts.cfg), "::" is refused -- it is reserved as the IPv6 twin of 0.0.0.0,
# "resolve the name" (#516) -- and xymonnet connects to an IPv6 address and
# reads its ping result.
#
# Checked here: xymongrep lists the IPv6 host and not the "::" one, with the
# reason; xymonnet's conn test reads an IPv6 ping result (from a stand-in
# pinger that answers "alive" for every address, so no ICMP is needed); and a
# TCP test connects to a listener on ::1 -- where ::1 cannot be bound, that
# last part is reported as not checked.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
require_bin XYMONGREP common/xymongrep
require_cc

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/bin"

# A listener: binds the numeric address given, prints its port, accepts forever.
cat >"$work/listen.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
int main(int argc, char **argv)
{
	struct addrinfo hints, *ai; struct sockaddr_storage a; socklen_t al = sizeof(a);
	char port[NI_MAXSERV]; int s, c;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_NUMERICHOST;
	if (argc != 2 || getaddrinfo(argv[1], "0", &hints, &ai) != 0) return 1;
	if ((s = socket(ai->ai_family, SOCK_STREAM, 0)) < 0) { perror("socket"); return 1; }
	if (bind(s, ai->ai_addr, ai->ai_addrlen) < 0) { perror("bind"); return 1; }
	if (listen(s, 5) < 0 || getsockname(s, (struct sockaddr *)&a, &al) < 0) { perror("listen"); return 1; }
	if (getnameinfo((struct sockaddr *)&a, al, NULL, 0, port, sizeof(port), NI_NUMERICSERV) != 0) return 1;
	printf("%s\n", port); fflush(stdout);
	for (;;) if ((c = accept(s, NULL, NULL)) >= 0) close(c);
}
EOF
"$CC" -o "$work/listen" "$work/listen.c" 2>"$work/cc.log" || fail "the listener does not compile: $(cat "$work/cc.log")"

# A pinger that finds every address alive, as fping prints it.
printf '#!/bin/sh\nwhile read ip; do echo "$ip is alive"; done\nexit 0\n' >"$work/home/bin/fping"
chmod +x "$work/home/bin/fping"

# run -- xymonnet's output for the hosts.cfg in place
run() {
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
		FPING="$work/home/bin/fping" FPINGOPTS="" \
		"$XYMONNET" --no-update --ping --report --timeout=3 2>&1 || true
}

# --- loading -----------------------------------------------------------------
cat >"$work/home/etc/hosts.cfg" <<'EOF'
2001:db8::10	v6far	# testip conn
::	nullhost	# conn
127.0.0.1	v4host	# conn
EOF
grepout=$(XYMONHOME="$work/home" HOSTSCFG="$work/home/etc/hosts.cfg" "$XYMONGREP" '*' 2>&1 || true)
grep -qE '^2001:db8::10[[:space:]]+v6far' <<<"$grepout" \
	|| fail "the IPv6 host was not loaded, or not with its address: $grepout"
grep -qE '^127\.0\.0\.1[[:space:]]+v4host' <<<"$grepout" || fail "the IPv4 host is no longer loaded: $grepout"
assert_not_contains "nullhost" "$(grep -v 'is not supported' <<<"$grepout")" "the \"::\" host was loaded"
assert_contains "Host nullhost: \"::\" is not supported as an address" "$grepout" "\"::\" was refused without saying why"

# --- ping --------------------------------------------------------------------
out=$(run)
assert_contains "v6far.conn green" "$out" "the IPv6 host's ping result was not read"
assert_contains "v4host.conn green" "$out" "the IPv4 host's ping result is no longer read"

# --- TCP ---------------------------------------------------------------------
"$work/listen" ::1 >"$work/port" 2>"$work/listen.err" &
register_cleanup "kill $! 2>/dev/null || true"
for _ in $(seq 1 50); do [ -s "$work/port" ] && break; kill -0 $! 2>/dev/null || break; sleep 0.1; done
if [ ! -s "$work/port" ]; then
	pass_partial "an IPv6 host in hosts.cfg is loaded, \"::\" is refused, and the IPv6 ping result is read" \
		"a TCP test over IPv6: this host has no IPv6 loopback ($(cat "$work/listen.err"))"
fi
printf '[demoport]\n   port %s\n' "$(cat "$work/port")" >"$work/home/etc/protocols.cfg"
printf '::1\tv6local\t# testip demoport\n' >"$work/home/etc/hosts.cfg"
out=$(run)
assert_contains "v6local.demoport green" "$out" "the TCP test did not reach the listener on ::1"

pass "an IPv6 host in hosts.cfg is loaded and tested over TCP and ping, and \"::\" is refused with its reason"
