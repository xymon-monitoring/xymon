#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/loadhosts-ip-length.sh
#
# Regression guard for the stack buffer overflow in knownhost() (lib/loadhosts.c).
#
# knownhost() writes the resolved IP into the caller's hostip buffer, which is
# char hostip[IP_ADDR_STRLEN] (16) at every caller. Two copies did it with an
# unbounded strcpy(). The dangerous one took the IP from hivals[XMH_IP] -- a
# field parsed straight out of a "hostinfo clone=" reply off the network -- so
# a clone response with an IP field longer than 15 characters overran the
# caller's 16-byte stack buffer. The fix bounds both copies to IP_ADDR_STRLEN.
#
# knownhost() pulls in the whole xtree/host-load machinery, so -- like
# tests/server/combostatus-overflow.sh -- this (1) binds to the real source:
# the bounded copies must be present and the raw strcpy(hostip, ...) gone, and
# (2) compiles a faithful copy of the bounded copy with a canary right past a
# 16-byte hostip and shows an over-length IP is truncated, not overrun. The
# unbounded form is deliberately NOT executed (it is the overflow under test).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/lib/loadhosts.c"
HDR="$ROOT/include/libxymon.h"
CC=${CC:-cc}

[ -f "$SRC" ] || skip "lib/loadhosts.c absent"

# (1) Bind to the real code: both copies bounded, no raw strcpy into hostip.
assert_contains "strncpy(hostip, hivals[XMH_IP], IP_ADDR_STRLEN - 1)" "$(cat "$SRC")" \
	"loadhosts.c knownhost() lost the bounded copy of the network clone IP into hostip"
assert_contains "strncpy(hostip, walk->ip, IP_ADDR_STRLEN - 1)" "$(cat "$SRC")" \
	"loadhosts.c knownhost() lost the bounded copy of the tree IP into hostip"
grep -Eq 'strcpy\(hostip,' "$SRC" \
	&& fail "loadhosts.c knownhost() still has an unbounded strcpy(hostip, ...) -- it can overrun the caller's hostip[IP_ADDR_STRLEN]"

# strncpy is only safe with the explicit NUL: it does not terminate when the
# source is IP_ADDR_STRLEN-1 or longer. Both bounded copies must terminate.
terms=$(grep -cF "hostip[IP_ADDR_STRLEN - 1] = '\\0';" "$SRC" || true)
[ "$terms" -eq 2 ] \
	|| fail "loadhosts.c knownhost() must NUL-terminate hostip after each bounded copy; found $terms of 2 -- a bare strncpy leaves hostip unterminated for a 15+ char source"

# The demo hardcodes IP_ADDR_STRLEN; pin it to the header so a change is noticed.
assert_contains "#define IP_ADDR_STRLEN 16" "$(cat "$HDR")" \
	"IP_ADDR_STRLEN is no longer 16; update loadhosts-ip-length.sh's demo to match"

# (2) Behavioural demo of the property, if we can compile.
command -v "$CC" >/dev/null 2>&1 \
	|| pass "loadhosts.c bounds the hostip copies (static check; no C compiler for the run)"

work=$(mktempdir)
cat >"$work/t.c" <<'CEOF'
#include <stdio.h>
#include <string.h>

#define IP_ADDR_STRLEN 16   /* mirrors include/libxymon.h; pinned by the test above */

/* Mirror knownhost()'s bounded copy into the caller's hostip[IP_ADDR_STRLEN]. */
static int copy_ip(const char *src)
{
	struct { char hostip[IP_ADDR_STRLEN]; char canary; } s;
	s.canary = '#';

	strncpy(s.hostip, src, IP_ADDR_STRLEN - 1);
	s.hostip[IP_ADDR_STRLEN - 1] = '\0';

	if (s.canary != '#')                    { fprintf(stderr, "canary clobbered -- copy overran hostip\n"); return 1; }
	if (strlen(s.hostip) >= IP_ADDR_STRLEN) { fprintf(stderr, "hostip not NUL-bounded: len=%zu\n", strlen(s.hostip)); return 1; }
	/* A source that fits must survive intact; one that does not must truncate,
	 * never overrun. */
	if (strlen(src) < IP_ADDR_STRLEN && strcmp(s.hostip, src) != 0) {
		fprintf(stderr, "short IP corrupted: '%s' != '%s'\n", s.hostip, src);
		return 1;
	}
	return 0;
}

int main(void)
{
	if (copy_ip("192.0.2.10") != 0) return 1;                 /* fits */
	if (copy_ip("255.255.255.255") != 0) return 1;            /* 15 chars, the max IPv4 */
	if (copy_ip("2001:0db8:85a3:0000:0000:8a2e:0370:7334") != 0) return 1; /* 39-char IPv6, would overrun a raw strcpy */
	if (copy_ip("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") != 0) return 1; /* pure over-length garbage */
	return 0;
}
CEOF

"$CC" -std=c99 -Wall -Wextra -Werror -o "$work/t" "$work/t.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "loadhosts ip-length probe did not compile"; }
"$work/t" || fail "loadhosts ip-length probe failed: an over-length IP was not safely truncated into hostip"

pass "knownhost() bounds an over-length IP into hostip[IP_ADDR_STRLEN] without overrunning it"
