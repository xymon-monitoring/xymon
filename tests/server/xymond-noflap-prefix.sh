#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-noflap-prefix.sh
#
# Regression guard for issue #293: the hosts.cfg "noflap=test1,test2,..." list
# matched test names by prefix rather than by name, so a listed entry silently
# covered every test whose name is a prefix of it. Among the 44 services in
# xymonnet/protocols.cfg, 18 pairs collide this way -- "noflap=imaps" also
# silenced imap, "noflap=ssh2" also silenced ssh, and so on.
#
# isset_noflap() is static in xymond/xymond.c and xymond has no library form,
# so the function is extracted from the real source at run time and compiled
# here. That way the test drives the shipped code rather than a copy of it,
# and cannot drift from it.
#
# Each assertion gets its own host, deliberately: isset_noflap() tokenises the
# pointer xmh_item() returns, so a list with a comma is truncated in the host
# record by the first evaluation (fixed separately in #276). Single-entry
# lists and one host per query keep this test independent of that.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

XYMOND_C="$ROOT/xymond/xymond.c"
[ -f "$XYMOND_C" ] || skip "xymond/xymond.c not present in this checkout"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-noflap-prefix.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

# Tags are spelled lowercase, as hosts.cfg(5) documents them, even though the
# key table in lib/loadhosts.c stores them upper-case.
cat > "$work/hosts.cfg" <<'EOF'
127.0.0.1 imaps1.example.com # conn noflap=imaps
127.0.0.1 imaps2.example.com # conn noflap=imaps
127.0.0.1 ssh2host.example.com # conn noflap=ssh2
127.0.0.1 barehost.example.com # conn noflap
EOF

# The real function, lifted from xymond.c and given a main().
{
	printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
	printf '#include "libxymon.h"\n'
	awk '/^static int isset_noflap/,/^}/' "$XYMOND_C"
	cat <<'EOF'
static int failures = 0;

static void expect(const char *label, int got, int want)
{
	if (got != want) {
		fprintf(stderr, "%s: got %d, want %d\n", label, got, want);
		failures++;
	}
}

int main(int argc, char *argv[])
{
	void *imaps1, *imaps2, *ssh2host, *barehost;

	if (argc != 2) { fprintf(stderr, "usage: %s hosts.cfg\n", argv[0]); return 2; }
	if (load_hostnames(argv[1], NULL, 1) == -1) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}

	imaps1   = hostinfo("imaps1.example.com");
	imaps2   = hostinfo("imaps2.example.com");
	ssh2host = hostinfo("ssh2host.example.com");
	barehost = hostinfo("barehost.example.com");
	if (!imaps1 || !imaps2 || !ssh2host || !barehost) {
		fprintf(stderr, "test hosts not found\n");
		return 2;
	}

	/* The regression: a listed entry must not cover its own prefixes. */
	expect("noflap=imaps must not suppress the imap test",
	       isset_noflap(imaps1, "imap", "imaps1"), 0);
	expect("noflap=ssh2 must not suppress the ssh test",
	       isset_noflap(ssh2host, "ssh", "ssh2host"), 0);

	/* Anti-vacuity: the listed test itself must still be suppressed, so the
	 * assertions above cannot be met by never suppressing anything. */
	expect("noflap=imaps must still suppress the imaps test",
	       isset_noflap(imaps2, "imaps", "imaps2"), 1);
	expect("a bare noflap must still suppress any test",
	       isset_noflap(barehost, "imap", "barehost"), 1);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
EOF
} > "$work/harness.c"

# Link flags come from the build's own Makefile: SSLLIBS is absent on a
# --no-ssl build, and carries the -L a non-standard OpenSSL prefix needs.
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" \
	$ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" "$work/hosts.cfg" 2>"$work/stderr.log" \
	|| fail "noflap prefix assertions failed:
$(cat "$work/stderr.log")"

pass "noflap= matches whole test names, not prefixes"
