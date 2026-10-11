#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/sockaddr-same-sender.sh
#
# sockaddr_same_sender() decides which connections xymond counts as one
# sender when it caps the connections that have sent nothing yet
# (tests/server/xymond-idle-connections.sh checks the cap itself). One IPv4
# address is one sender, and so is one IPv6 /64: a machine is usually given a
# whole /64, and counting single IPv6 addresses would let it pass for millions
# of senders and escape the cap.
#
# Checked on the real lib/ipaccess.c, compiled into a harness: two addresses
# in one /64 are one sender, the next /64 is another; IPv4 is compared whole;
# an IPv4-mapped IPv6 address is the IPv4 address it carries; IPv4 and IPv6
# are never the same sender. Addresses only -- loopback offers no second
# address in ::1's /64 to connect from without root.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
[ -f "$ROOT/lib/libxymonclient.a" ] && [ -f "$ROOT/lib/libxymonclientcomm.a" ] \
	|| skip "the client libraries are not built"

work=$(mktempdir)

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "libxymon.h"

/* "same" or "different" for the two addresses given as text */
int main(int argc, char *argv[])
{
	struct sockaddr_storage a, b;

	if ((argc != 3) || !text_sockaddr(argv[1], &a) || !text_sockaddr(argv[2], &b)) return 2;
	printf("%s\n", sockaddr_same_sender((struct sockaddr *)&a, (struct sockaddr *)&b) ? "same" : "different");
	return 0;
}
EOF

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# The client archives use PCRE only in a localclient tree, as in
# netrc-password.sh, so a plain client tree links none.
case $harness_cflags in
	*-DLOCALCLIENT*) ;;
	*-DCLIENTONLY*) pcre_libs= ;;
esac

# The client archives, which a server build makes too, so the test runs in
# both. lib/ipaccess.c before them, so the harness runs the source as it is
# rather than a possibly older ipaccess.o.
# shellcheck disable=SC2086  # the flags are a list of words
"$CC" $harness_cflags -iquote "$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/ipaccess.c" "$ROOT/lib/libxymonclientcomm.a" "$ROOT/lib/libxymonclient.a" \
	$harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"

check() {
	local a=$1 b=$2 want=$3 why=$4 got
	got=$("$work/harness" "$a" "$b") || fail "the harness could not read $a or $b"
	assert_equal "$want" "$got" "$a and $b: $why"
}

check 2001:db8:1:2::1 2001:db8:1:2::1 same "one IPv6 address is one sender"
check 2001:db8:1:2::1 2001:db8:1:2:ffff:ffff:ffff:ffff same "two addresses in one /64 must be one sender, or a host escapes the cap by changing address"
check 2001:db8:1:2::1 2001:db8:1:3::1 different "the next /64 is another sender"
check 2001:db8:1:2::1 2001:db8:1:2:: same "a /64's first address belongs to it"
check 192.0.2.1 192.0.2.1 same "one IPv4 address is one sender"
check 192.0.2.1 192.0.2.2 different "IPv4 is compared whole: the next address is another sender"
check ::ffff:192.0.2.1 192.0.2.1 same "an IPv4-mapped address is the IPv4 address it carries"
check ::ffff:192.0.2.1 192.0.2.2 different "an IPv4-mapped address is compared whole, as IPv4"
check 192.0.2.1 ::c000:201 different "an IPv6 address is never an IPv4 sender, whatever its bytes"

pass "one IPv4 address or one IPv6 /64 is one sender, an IPv4-mapped address counts as IPv4, and the two families never match"
