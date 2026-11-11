#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/split-hostport.sh
#
# split_hostport(), now in libxymoncomm for every program that reads an
# IP:PORT, reads "host:port", "[IPv6]:port", "[IPv6]" and a bare IPv6
# address: the host without brackets, and the port only when one is given.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "libxymon.h"

/* split S, starting from port 0; "host port" */
static int check(const char *s, const char *want)
{
	char buf[100], got[120], *host;
	int port = 0;

	snprintf(buf, sizeof(buf), "%s", s);
	host = split_hostport(buf, &port);
	snprintf(got, sizeof(got), "%s %d", host, port);
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
	return fails;
}
EOF
"$CC" $(xymon_cflags "$ROOT") -o "$work/harness" "$work/harness.c" \
	"$(xymon_lib "$ROOT" libxymoncomm.a)" $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"
out=$("$work/harness" 2>&1) || fail "$out"

pass "split_hostport() reads host:port, [IPv6]:port, [IPv6] and a bare IPv6 address"
