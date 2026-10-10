#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/sendmsg-ipv6.sh
#
# The client reaches xymond over IPv6 as well as IPv4 (lib/sendmsg.c).
#
# The recipient may be "[IPv6]:port", a bare IPv6 address, "IPv4:port" or a
# name; a name that resolves to several addresses is tried address by
# address until one connects. The client used to split the recipient at its
# first ':' and resolve it with inet_aton()/gethostbyname(), which know IPv4
# only, and it connected to the first address alone.
#
# The real xymon client sends a query to tests/lib/fake-xymond.c, listening
# on ::1 or on 127.0.0.1, and must print the reply -- directly, through an
# http://[::1]:port/ recipient, and through a proxy at http://[::1]:port. The fallback is checked
# with "localhost": when the resolver gives it both ::1 and 127.0.0.1, a
# listener on either one alone must still be reached, whichever comes first.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_bin XYMON common/xymon
require_cc

work=$(mktempdir)
pids=""
register_cleanup 'for p in $pids; do kill "$p" 2>/dev/null || true; done'

"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc.log" \
	|| fail "fake-xymond does not compile: $(cat "$work/cc.log")"
printf 'reply from the fake xymond\n' >"$work/reply"

# start_listener ADDRESS -- a fake xymond on ADDRESS; its port in $port.
# Returns 1 when ADDRESS cannot be bound here.
start_listener() {
	local out=$work/port.$1 i
	"$work/fake-xymond" "$work/reply" "$1" >"$out" 2>"$out.err" &
	pids="$pids $!"
	for i in $(seq 1 50); do
		[ -s "$out" ] && break
		kill -0 "$!" 2>/dev/null || return 1
		sleep 0.1
	done
	[ -s "$out" ] || return 1
	port=$(cat "$out")
}

# ask RECIPIENT [XYMONDPORT] -- what the client printed for a query
ask() {
	env XYMONDPORT="${2:-1984}" "$XYMON" "$1" "query sendmsg.test" 2>"$work/client.err" || true
}

start_listener 127.0.0.1 || fail "fake-xymond cannot listen on 127.0.0.1: $(cat "$work/port.127.0.0.1.err")"
port4=$port

got=$(ask "127.0.0.1:$port4")
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client no longer reaches 127.0.0.1:$port4 (printed '$got'; $(cat "$work/client.err"))"

start_listener ::1 || pass_partial "the client reaches xymond at IPv4:port" \
	"IPv6: this host has no IPv6 loopback, ::1 cannot be bound ($(cat "$work/port.::1.err"))"
port6=$port

got=$(ask "[::1]:$port6")
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach [::1]:$port6 (printed '$got'; $(cat "$work/client.err"))"

got=$(ask "::1" "$port6")
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach a bare ::1 on XYMONDPORT $port6 (printed '$got'; $(cat "$work/client.err"))"

# The http:// transport and the proxy setting go through the same parser: a
# web server on [::1] (xymoncgimsg.cgi), and the same server as a proxy.
printf 'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nreply from the fake xymond\n' >"$work/httpreply"
"$work/fake-xymond" "$work/httpreply" ::1 >"$work/port.http" 2>"$work/port.http.err" &
pids="$pids $!"
for _ in $(seq 1 50); do [ -s "$work/port.http" ] && break; sleep 0.1; done
[ -s "$work/port.http" ] || fail "the HTTP stand-in did not start on ::1: $(cat "$work/port.http.err")"
porth=$(cat "$work/port.http")

got=$(ask "http://[::1]:$porth/xymon-cgi/xymoncgimsg.cgi")
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach http://[::1]:$porth/ (printed '$got'; $(cat "$work/client.err"))"

got=$(env http_proxy="http://[::1]:$porth" "$XYMON" "http://xymon.example.invalid/xymon-cgi/xymoncgimsg.cgi" "query sendmsg.test" 2>"$work/client.err" || true)
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach its proxy at http://[::1]:$porth (printed '$got'; $(cat "$work/client.err"))"

# Which families the resolver gives "localhost", in its order.
cat >"$work/families.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
int main(void)
{
	struct addrinfo hints, *ai, *p;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo("localhost", "1984", &hints, &ai) != 0) return 1;
	for (p = ai; p; p = p->ai_next) printf("%s\n", (p->ai_family == AF_INET6) ? "6" : "4");
	freeaddrinfo(ai);
	return 0;
}
EOF
"$CC" -o "$work/families" "$work/families.c" 2>"$work/cc2.log" \
	|| fail "the resolver probe does not compile: $(cat "$work/cc2.log")"
families=$("$work/families" | awk '!seen[$0]++' | tr -d '\n')

case $families in
	46|64) ;;
	*) pass_partial "the client reaches xymond at [IPv6]:port, a bare IPv6 address and IPv4:port" \
		"the fallback across a name's addresses: \"localhost\" resolves to one family only here ($families)" ;;
esac

# A listener on each family alone, at a port the other family does not
# listen on: whichever address the resolver lists first, one of the two
# needs the client to move on to the next.
got=$(ask "localhost:$port6")
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach localhost:$port6, listened on by ::1 only (resolver order $families; printed '$got'; $(cat "$work/client.err"))"
got=$(ask "localhost:$port4")
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach localhost:$port4, listened on by 127.0.0.1 only (resolver order $families; printed '$got'; $(cat "$work/client.err"))"

pass "the client reaches xymond at [IPv6]:port, a bare IPv6 address and IPv4:port, through http://[IPv6]:port/ and an IPv6 proxy, and tries each address of a name until one connects"
