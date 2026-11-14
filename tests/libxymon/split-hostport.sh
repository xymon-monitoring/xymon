#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/split-hostport.sh
#
# split_hostport() reads "host:port", "[IPv6]:port", "[IPv6]" and a bare IPv6
# address, and refuses a malformed one.
#
# It took anything: "[::1]junk" became ::1 on the default port, "host:junk"
# port 0, and "host:70000" a port that htons() then truncated, so a typo
# bound or connected somewhere other than intended. Now each of those gives
# NULL, and the string is left as it was; sendmsg then passes such a recipient
# on unsplit, so the xymon client reports it and does not crash on the NULL.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMON common/xymon
work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "libxymon.h"

/* split S, starting from port 0; "host port", or "NULL" */
static int check(const char *s, const char *want)
{
	char buf[100], got[120], *host;
	int port = 0;

	snprintf(buf, sizeof(buf), "%s", s);
	host = split_hostport(buf, &port);
	if (host) snprintf(got, sizeof(got), "%s %d", host, port);
	else if (strcmp(buf, s) != 0) snprintf(got, sizeof(got), "NULL, but the string changed to %s", buf);
	else snprintf(got, sizeof(got), "NULL");
	if (strcmp(got, want) == 0) return 0;
	printf("split_hostport(\"%s\") = %s, want %s\n", s, got, want);
	return 1;
}

int main(void)
{
	int fails = 0;

	fails += check("10.0.0.1:1985", "10.0.0.1 1985");
	fails += check("10.0.0.1", "10.0.0.1 0");
	fails += check("xymon.example.com:65535", "xymon.example.com 65535");
	fails += check("[::1]:1985", "::1 1985");
	fails += check("[::1]", "::1 0");
	fails += check("::1", "::1 0");
	fails += check("2001:db8::5", "2001:db8::5 0");
	fails += check(":1985", " 1985");
	fails += check("[::1]junk", "NULL");
	fails += check("[::1]:junk", "NULL");
	fails += check("[::1", "NULL");
	fails += check("host:junk", "NULL");
	fails += check("host:", "NULL");
	fails += check("host:0", "NULL");
	fails += check("host:70000", "NULL");
	fails += check("host:+80", "NULL");
	fails += check("host:80x", "NULL");
	return fails;
}
EOF
"$CC" $(xymon_cflags "$ROOT") -o "$work/harness" "$work/harness.c" \
	"$(xymon_lib "$ROOT" libxymoncomm.a)" $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"
out=$("$work/harness" 2>&1) || fail "$out"

# The xymon client, through sendmsg, with a malformed recipient: a standard one
# and an HTTP one, then a good recipient through a malformed http_proxy. Each
# fails cleanly (rc 1 to 20, the XYMONSEND codes) instead of crashing, and the
# malformed proxy is still the one used, not quietly dropped.
send() {	# send RECIPIENT [PROXY] -- the client's output; its rc in $rc
	rc=0
	if [ -n "${2:-}" ]; then
		out=$(http_proxy=$2 XYMON_TIMEOUT=2 "$XYMON" --debug "$1" ping 2>&1) || rc=$?
	else
		out=$(unset http_proxy; XYMON_TIMEOUT=2 "$XYMON" "$1" ping 2>&1) || rc=$?
	fi
}
for r in "127.0.0.1:junk" "[::1]junk" "http://[::1]junk/"; do
	send "$r"
	[ "$rc" -ge 1 ] && [ "$rc" -le 20 ] || fail "sending to the malformed recipient $r ended with rc $rc, not an error: $out"
done
send "http://127.0.0.1:1/" "127.0.0.1:junk"
[ "$rc" -ge 1 ] && [ "$rc" -le 20 ] || fail "sending through the malformed proxy 127.0.0.1:junk ended with rc $rc, not an error: $out"
assert_contains "xymonproxyhost = 127.0.0.1:junk" "$out" "a malformed http_proxy was dropped instead of used"

pass "split_hostport() reads host:port, [IPv6]:port, [IPv6] and a bare IPv6 address, and refuses a malformed one untouched; sendmsg reports such a recipient without crashing"
