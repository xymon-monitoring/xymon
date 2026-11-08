#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/ping-result-by-address.sh
#
# A ping result is matched to its host by address, not by the spelling of
# the address (#765).
#
# xymonnet feeds the pinger the address as written in hosts.cfg and reads
# back one result line per target. fping with -A, the shipped FPINGOPTS,
# reports a target in its own shortest spelling: "0:0:0:0:0:0:0:1" comes
# back as "::1". The result used to be matched to the host by strcmp(), so
# an IPv6 address written any other way than fping writes it never got its
# result and the host's conn was red while it answered. IPv4 has one
# spelling and never hit this.
#
# The pinger here answers "alive" for every address, spelled the way
# inet_ntop() spells it, as fping -A does; no ICMP is needed. Checked: an
# IPv6 host written long, one written with leading zeros, a conn=worst extra
# target written long, and an IPv4 host are all green; a host whose address
# does not answer is still red, so the match is by address and not by
# "any alive line".

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/bin"

# A pinger that prints every target it reads in inet_ntop()'s spelling, as
# fping -A does, alive unless the address is 2001:db8::dead.
cat >"$work/fping.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
int main(void)
{
	char l[256], out[INET6_ADDRSTRLEN];
	struct in_addr a4; struct in6_addr a6, dead;
	inet_pton(AF_INET6, "2001:db8::dead", &dead);
	while (fgets(l, sizeof(l), stdin)) {
		l[strcspn(l, "\n")] = '\0';
		if (inet_pton(AF_INET, l, &a4) == 1) inet_ntop(AF_INET, &a4, out, sizeof(out));
		else if (inet_pton(AF_INET6, l, &a6) == 1) inet_ntop(AF_INET6, &a6, out, sizeof(out));
		else continue;
		if (memcmp(&a6, &dead, sizeof(a6)) == 0 && strchr(l, ':')) printf("%s is unreachable\n", out);
		else printf("%s is alive\n", out);
	}
	return 0;
}
EOF2
"$CC" -o "$work/home/bin/fping" "$work/fping.c" 2>"$work/cc.log" || fail "the stand-in pinger does not compile: $(cat "$work/cc.log")"

cat >"$work/home/etc/hosts.cfg" <<'EOF2'
0:0:0:0:0:0:0:1	v6long	# testip conn
0000:0db8::0001	v6zeros	# testip conn
2001:db8::1	v6short	# testip conn
2001:db8::2	v6extra	# testip conn=worst,2001:0db8:0000:0000:0000:0000:0000:0003
2001:0db8::dead	v6down	# testip conn
127.0.0.1	v4host	# testip conn
EOF2

out=$(XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
	FPING="$work/home/bin/fping" FPINGOPTS="-Ae" \
	"$XYMONNET" --no-update --ping --report --timeout=3 2>&1 || true)

assert_match 'status[^ ]* v6long\.conn green' "$out" "an IPv6 address written long did not get its ping result"
assert_match 'status[^ ]* v6zeros\.conn green' "$out" "an IPv6 address written with leading zeros did not get its ping result"
assert_match 'status[^ ]* v6short\.conn green' "$out" "an IPv6 address written as the pinger spells it no longer gets its result"
assert_match 'status[^ ]* v6extra\.conn green' "$out" "a conn=worst target written long did not get its ping result"
assert_match 'status[^ ]* v6down\.conn red' "$out" "a host the pinger found unreachable is not red: results are not matched by address"
assert_match 'status[^ ]* v4host\.conn green' "$out" "the IPv4 host's ping result is no longer read"

pass "a ping result is matched to its host by address, whatever the spelling of either side"
