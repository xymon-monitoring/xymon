#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-noflap-list.sh
#
# Regression guard for the list form of the hosts.cfg "noflap=test1,test2,..."
# tag silently losing effect for every test after the first one evaluated.
#
# xmh_item() for XMH_NOFLAP returns a pointer into the host record's own tag
# buffer rather than a copy, so xymond's isset_noflap() tokenizing it with
# strtok() overwrote the list's first comma with a NUL and truncated the
# stored tag permanently. With isset_noflap() running once per incoming
# status message, only the first test ever kept its flap suppression:
#
#   before any call    raw tags: [conn] [noflap=web,cpu,disk]
#   isset_noflap(web)  = 1
#   after first call   raw tags: [conn] [noflap=web]
#   isset_noflap(cpu)  = 0
#   isset_noflap(disk) = 0
#
# The bare "noflap" flag form was unaffected: lib/loadhosts.c's XMH_NOFLAP
# case mirrors flag semantics for it, so isset_noflap() short-circuits
# before tokenizing.
#
# Since isset_noflap() is static and xymond has no library form, the test
# extracts the real function from xymond.c into a generated translation unit,
# then appends assertions that drive it against real host records loaded
# through the real lib/loadhosts.c.
#
# Not fixed here, and deliberately left alone: xymongen/loaddata.c tokenizes
# the XMH_COMPACT value in place the same way. No user-visible symptom has
# been demonstrated for it (xymongen normally visits each host once), so it
# is out of this change's scope.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

XYMOND_C="$ROOT/xymond/xymond.c"
[ -f "$XYMOND_C" ] || skip "xymond/xymond.c not present in this checkout"

body=$(awk '/^static int isset_noflap/,/^}/' "$XYMOND_C")
[ -n "$body" ] || fail "could not locate isset_noflap() in xymond/xymond.c"
CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-noflap-list.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

{
	printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
	printf '#include "libxymon.h"\n'
	printf '%s\n' "$body"
	cat "$here/xymond-noflap-list-harness.c"
} > "$work/harness.c"

# Tags are spelled lowercase here because that is what hosts.cfg(5)
# documents ("noflap[=test1,test2,...]"), even though the key table in
# lib/loadhosts.c stores them upper-case.
cat > "$work/hosts.cfg" <<'EOF'
127.0.0.1 barehost.example.com # conn noflap
127.0.0.1 listhost.example.com # conn noflap=web,cpu,disk
127.0.0.1 recordhost.example.com # conn noflap=web,cpu
EOF

"$CC" -iquote "$ROOT/include" -iquote "$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" \
	$ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" "$work/hosts.cfg" 2>"$work/stderr.log" \
	|| fail "noflap assertions failed:
$(cat "$work/stderr.log")"

pass "noflap= list keeps working across repeated evaluations and leaves the host record intact"
