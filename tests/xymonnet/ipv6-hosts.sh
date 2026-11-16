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
# last part is reported as not checked. A 0.0.0.0 host whose name has only an
# IPv6 address is resolved to it and tested there; xymonnet used to look up
# IPv4 addresses only. And a URL whose host is an IPv4 short form (127.1) is
# taken as an address, as inet_aton() took it, not looked up as a name.

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
		"$XYMONNET" --no-update --ping --report --timeout=3 "$@" 2>&1 || true
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

# --- a URL host that is an IPv4 short form -----------------------------------
# An address, not a name to look up: c-ares does not read short forms
printf '0.0.0.0\tlocalhost\t# http://127.1:1/\n' >"$work/home/etc/hosts.cfg"
out=$(run --debug)
assert_contains "Adding tcp test IP=127.1, port=1" "$out" "the URL host 127.1 was not tested"
assert_not_contains "DNS lookup failed for 127.1" "$out" "the URL host 127.1 was looked up as a name"
assert_not_contains "DNS error" "$out" "the URL host 127.1 gave a DNS error"

# --- the system resolver (--no-ares) -----------------------------------------
# A name that resolves and one that does not, through getaddrinfo(): each
# lookup counted once in the DNS statistics, and the failure logged once
printf '[demoport]\n   port 1\n' >"$work/home/etc/protocols.cfg"
printf '0.0.0.0\tlocalhost\t# demoport\n0.0.0.0\tnosuch.invalid\t# demoport\n' >"$work/home/etc/hosts.cfg"
out=$(run --no-ares --dnslog="$work/dns.log")
stats=$(sed -n '/DNS statistics/,/dnsresolve/p' <<<"$out" | tr -s ' ')
if grep -q "successful : 2" <<<"$stats" && ! grep -q 'nosuch.invalid' "$work/dns.log"; then
	# A resolver that answers for any name (NXDOMAIN rewriting, a wildcard
	# in a search domain): there is no failure to count
	nores_partial="the --no-ares counts: this resolver answers for nosuch.invalid"
else
	nores_partial=
	for want in "resolved : 2" "successful : 1" "failed : 1"; do
		assert_contains "$want" "$stats" "the --no-ares lookups were not counted once each"
	done
	[ "$(grep -c 'nosuch.invalid' "$work/dns.log")" = 1 ] \
		|| fail "the failed --no-ares lookup was not logged once to --dnslog: $(cat "$work/dns.log")"
fi

# --- TCP ---------------------------------------------------------------------
"$work/listen" ::1 >"$work/port" 2>"$work/listen.err" &
register_cleanup "stop_process $!"
for _ in $(seq 1 50); do [ -s "$work/port" ] && break; kill -0 $! 2>/dev/null || break; sleep 0.1; done
if [ ! -s "$work/port" ]; then
	pass_partial "an IPv6 host in hosts.cfg is loaded, \"::\" is refused, and the IPv6 ping result is read" \
		"a TCP test over IPv6: this host has no IPv6 loopback ($(cat "$work/listen.err"))"
fi
printf '[demoport]\n   port %s\n' "$(cat "$work/port")" >"$work/home/etc/protocols.cfg"
printf '::1\tv6local\t# testip demoport\n' >"$work/home/etc/hosts.cfg"
out=$(run)
assert_contains "v6local.demoport green" "$out" "the TCP test did not reach the listener on ::1"

# --- a name with only an IPv6 address ----------------------------------------
# A 0.0.0.0 line asks for its name to be resolved: here a name /etc/hosts
# gives to ::1 alone, such as Debian's ip6-localhost or Fedora's localhost6.
v6name=$(awk '!/^[ \t]*#/ && NF > 1 {
	for (i = 2; i <= NF && $i !~ /^#/; i++) if ($1 == "::1") v6[$i] = 1; else other[$i] = 1
} END { for (n in v6) if (!(n in other)) { print n; exit } }' /etc/hosts 2>/dev/null || true)
if [ -z "$v6name" ]; then
	pass_partial "an IPv6 host in hosts.cfg is loaded and tested over TCP and ping, and \"::\" is refused with its reason" \
		"a name with only an IPv6 address: /etc/hosts has no name for ::1 alone"
fi
printf '0.0.0.0\t%s\t# demoport\n' "$v6name" >"$work/home/etc/hosts.cfg"
out=$(run --debug)
got=$(first_line "$(sed -n "s/.*Got DNS result for host $v6name : //p" <<<"$out")")
case $got in
*.*.*.*)	# the system resolver (DNS, or a stub such as systemd-resolved) gives it an IPv4 address too
	pass_partial "an IPv6 host in hosts.cfg is loaded and tested over TCP and ping, and \"::\" is refused with its reason" \
		"a name with only an IPv6 address: this system resolves $v6name to $got as well" ;;
esac
# A DNS server that does not answer: the IPv4 lookup times out, and by design
# no IPv6 lookup follows, whatever /etc/hosts says
if [ -z "$got" ] && grep -q "DNS lookup failed for $v6name.*Timeout" <<<"$out"; then
	pass_partial "an IPv6 host in hosts.cfg is loaded and tested over TCP and ping, and \"::\" is refused with its reason" \
		"a name with only an IPv6 address: the DNS server does not answer here"
fi
[ "$got" = ::1 ] || fail "$v6name, a 0.0.0.0 host whose name has only an IPv6 address, was not resolved to ::1 (got '${got:-nothing}')"
assert_contains "${v6name//./,}.demoport green" "$out" \
	"$v6name, a 0.0.0.0 host whose name has only an IPv6 address, was not resolved to it"

claim="an IPv6 host in hosts.cfg is loaded and tested over TCP and ping, a 0.0.0.0 host whose name has only an IPv6 address is resolved to it, and \"::\" is refused with its reason"
if [ -n "$nores_partial" ]; then pass_partial "$claim" "$nores_partial"; else pass "$claim"; fi
