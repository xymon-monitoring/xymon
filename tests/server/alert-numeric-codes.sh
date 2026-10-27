#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alert-numeric-codes.sh
#
# An alert script's MACHIP and BBNUMERIC carry a host's IPv4 address, or zeros.
#
# Both encode the address as four three-digit fields, and do_alert.c sizes
# their buffers for that. They were filled with sscanf("%d.%d.%d.%d"), which
# reads an IPv6 address such as 2001:db8::1 as 2001 and three zeros: four
# digits where three fit, one byte past the end of MACHIP. Only an IPv4
# address may fill them now; any other leaves them zero.
#
# The harness sends one red alert through the real send_alert() to a SCRIPT
# recipient that records the two variables.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
require_gnu_make

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
harness="$work/harness"

"$XYMON_MAKE" -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# -iquote xymond: the harness #includes do_alert.c.
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # deliberate word-splitting, as the neighbouring tests do
"$CC" $harness_cflags -iquote "$ROOT/xymond" -o "$harness" "$here/alert-numeric-codes-harness.c" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

printf '127.0.0.1  testhost  # conn\n' >"$work/hosts.cfg"
# shellcheck disable=SC2016  # the script expands these, not this shell
printf '#!/bin/sh\necho "MACHIP=$MACHIP BBNUMERIC=$BBNUMERIC" >"%s/codes"\n' "$work" >"$work/record.sh"
chmod +x "$work/record.sh"
printf 'HOST=testhost\n\tSCRIPT %s somebody FORMAT=SCRIPT\n' "$work/record.sh" >"$work/alerts.cfg"

# codes ADDRESS -- what the script recorded for an alert carrying ADDRESS
codes() {
	rm -f "$work/codes"
	XYMONHOME="$work" XYMONTMP="$work" "$harness" "$work/hosts.cfg" "$work/alerts.cfg" "$1" \
		>"$work/run.out" 2>"$work/run.err" || fail "the harness did not run: $(cat "$work/run.err")"
	for _ in $(seq 1 50); do [ -s "$work/codes" ] && break; sleep 0.1; done
	[ -s "$work/codes" ] || fail "the alert script never ran for $1: $(cat "$work/run.err")"
	cat "$work/codes"
}

got=$(codes 192.168.1.2)
assert_contains "MACHIP=192168001002 " "$got" "an IPv4 address no longer gives its code"
# BBNUMERIC is the service code, the address code, then the alert cookie
grep -qE 'BBNUMERIC=[0-9]{3}192168001002[0-9]' <<<"$got" || fail "an IPv4 address no longer gives its BBNUMERIC: $got"

got=$(codes 2001:db8::1)
assert_contains "MACHIP=000000000000 " "$got" "an IPv6 address filled the IPv4 code"
grep -qE 'BBNUMERIC=[0-9]{3}000000000000[0-9]' <<<"$got" || fail "an IPv6 address filled BBNUMERIC's IPv4 part: $got"

pass "an alert script's MACHIP and BBNUMERIC carry the host's IPv4 address, and zeros for an IPv6 one"
