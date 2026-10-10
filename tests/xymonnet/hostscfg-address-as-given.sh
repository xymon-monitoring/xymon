#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/hostscfg-address-as-given.sh
#
# xymonnet tests the hosts.cfg address as given, and a line's address serves
# its URL tests (#516, rules 1 and 3).
#
# xymonnet used to resolve every host's name and test the answer, using the
# hosts.cfg address only when the name did not resolve; a URL test always
# resolved its own name. Now only 0.0.0.0 asks for a lookup: any other address
# is tested as written, with no lookup at all, and a URL test on that line
# connects to it while Host: keeps the URL's name.
#
#   A  127.0.0.1 nosuchname.invalid    green, and no name resolved at all
#   B  127.0.0.2 localhost             reaches a listener on 127.0.0.2 only,
#                                      although DNS says 127.0.0.1 -- where
#                                      127.0.0.2 exists (Linux); else not checked
#   C  127.0.0.1 web, URL for nosuchname.invalid
#                                      fetched from 127.0.0.1, Host: the URL's name
#   D  0.0.0.0 localhost               still resolved
#   E  127.0.0.2 web, URL with its own "=127.0.0.1"
#                                      fetched from 127.0.0.1: "=IP" beats the line

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"

# listen ADDRESS FILE -- accept forever on ADDRESS; print the port. Each
# connection's request is appended to FILE, and answered as HTTP.
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
	char port[NI_MAXSERV], buf[4096]; int s, c; ssize_t n; FILE *f;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_NUMERICHOST;
	if (argc != 3 || getaddrinfo(argv[1], "0", &hints, &ai) != 0) return 1;
	if ((s = socket(ai->ai_family, SOCK_STREAM, 0)) < 0) { perror("socket"); return 1; }
	if (bind(s, ai->ai_addr, ai->ai_addrlen) < 0) { perror("bind"); return 1; }
	if (listen(s, 5) < 0 || getsockname(s, (struct sockaddr *)&a, &al) < 0) { perror("listen"); return 1; }
	if (getnameinfo((struct sockaddr *)&a, al, NULL, 0, port, sizeof(port), NI_NUMERICSERV) != 0) return 1;
	printf("%s\n", port); fflush(stdout);
	for (;;) {
		if ((c = accept(s, NULL, NULL)) < 0) continue;
		if ((n = read(c, buf, sizeof(buf) - 1)) > 0) {
			buf[n] = 0;
			if ((f = fopen(argv[2], "a")) != NULL) { fputs(buf, f); fclose(f); }
		}
		(void)!write(c, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nok\n", 46);
		close(c);
	}
}
EOF
"$CC" -o "$work/listen" "$work/listen.c" 2>"$work/cc.log" || fail "the listener does not compile: $(cat "$work/cc.log")"

# start ADDRESS NAME -- a listener on ADDRESS; its port in $port. 1 if ADDRESS cannot be bound.
start() {
	"$work/listen" "$1" "$work/req.$2" >"$work/port.$2" 2>"$work/err.$2" &
	register_cleanup "kill $! 2>/dev/null || true"
	for _ in $(seq 1 50); do
		[ -s "$work/port.$2" ] && break
		kill -0 $! 2>/dev/null || return 1
		sleep 0.1
	done
	[ -s "$work/port.$2" ] || return 1
	port=$(cat "$work/port.$2")
}

# run HOSTSLINE -- xymonnet's output for a hosts.cfg of that one line
run() {
	printf '%s\n' "$1" >"$work/home/etc/hosts.cfg"
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
		"$XYMONNET" --no-update --noping --report --timeout=3 2>&1 || true
}
resolved() { sed -n 's/^ # hostnames resolved  : *\([0-9]*\).*/\1/p' <<<"$1"; }

start 127.0.0.1 lo || fail "no listener on 127.0.0.1: $(cat "$work/err.lo")"
port1=$port
printf '[demoport]\n   port %s\n' "$port1" >"$work/home/etc/protocols.cfg"

# A: the address as given, and no lookup
out=$(run "127.0.0.1 nosuchname.invalid # demoport")
assert_contains "nosuchname,invalid.demoport green" "$out" "a host was not tested at its hosts.cfg address"
[ "$(resolved "$out")" = 0 ] || fail "a hostname was resolved for a line that gives an address ($(resolved "$out") resolved)"

# C: the line's address serves its URL test, which keeps its name as Host:
out=$(run "127.0.0.1 web # http://nosuchname.invalid:$port1/")
assert_contains "web.http green" "$out" "a URL test on a line with an address did not connect to that address"
grep -qi '^Host: nosuchname.invalid' "$work/req.lo" \
	|| fail "the URL test did not send its own name as Host: $(cat "$work/req.lo")"
[ "$(resolved "$out")" = 0 ] || fail "the URL's name was resolved although its line gives an address"

# E: a URL's own "=IP" beats the line's address (nothing listens on 127.0.0.2:$port1)
out=$(run "127.0.0.2 web # http://nosuchname.invalid:$port1=127.0.0.1/")
assert_contains "web.http green" "$out" "a URL's own \"=127.0.0.1\" was overridden by its line's address"

# D: 0.0.0.0 still asks for the name to be resolved
out=$(run "0.0.0.0 localhost # demoport")
assert_contains "localhost.demoport green" "$out" "a 0.0.0.0 line was not resolved and tested"
[ "$(resolved "$out")" -ge 1 ] || fail "a 0.0.0.0 line was not resolved"

# B: an address DNS contradicts is still the one tested
if start 127.0.0.2 lo2; then
	printf '[demoport]\n   port %s\n' "$port" >"$work/home/etc/protocols.cfg"
	out=$(run "127.0.0.2 localhost # demoport")
	assert_contains "localhost.demoport green" "$out" \
		"the listener on 127.0.0.2, the hosts.cfg address, was not reached; DNS says localhost is 127.0.0.1"
else
	pass_partial "xymonnet tests the hosts.cfg address as given, with no lookup, a line's address serves its URL tests, and 0.0.0.0 is resolved" \
		"an address DNS contradicts: 127.0.0.2 cannot be bound here ($(cat "$work/err.lo2"))"
fi

pass "xymonnet tests the hosts.cfg address as given, even where DNS disagrees, with no lookup; a line's address serves its URL tests, which keep their name, unless a URL gives its own; 0.0.0.0 is resolved"
