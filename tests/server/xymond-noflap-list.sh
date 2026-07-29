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
# Two halves, because isset_noflap() is static in xymond/xymond.c and xymond
# has no library form to link against:
#   1. a source assertion that xymond.c still copies before tokenizing --
#      this is what actually guards the fix in the real file;
#   2. a C harness (xymond-noflap-list-harness.c) that keeps an identical
#      copy of the function and drives it against real host records loaded
#      through the real lib/loadhosts.c, proving the copy-first logic gives
#      the right answers and leaves the record intact.
# The source assertion is what keeps the harness's copy from drifting from
# the original.
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

# --- 1. Source assertion: the real isset_noflap() must not hand the pointer
# xmh_item() returned straight to strtok(). Scoped to the function body so an
# unrelated strtok() elsewhere in xymond.c cannot mask a regression here.
body=$(awk '/^static int isset_noflap/,/^}/' "$XYMOND_C")
[ -n "$body" ] || fail "could not locate isset_noflap() in xymond/xymond.c"

assert_contains 'strdup(' "$body" \
	"isset_noflap() must copy the XMH_NOFLAP value before tokenizing it -- xmh_item() \
returns a pointer into the host record's own tag buffer, and strtok() would truncate \
the stored noflap list at its first comma"

assert_not_contains 'strtok(dstr' "$body" \
	"isset_noflap() still tokenizes the xmh_item() result directly (strtok(dstr, ...)), \
which corrupts the host record's noflap list"

# --- 2. Behavioural half.
CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-noflap-list.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

# Tags are spelled lowercase here because that is what hosts.cfg(5)
# documents ("noflap[=test1,test2,...]"), even though the key table in
# lib/loadhosts.c stores them upper-case.
cat > "$work/hosts.cfg" <<'EOF'
127.0.0.1 barehost.example.com # conn noflap
127.0.0.1 listhost.example.com # conn noflap=web,cpu,disk
127.0.0.1 recordhost.example.com # conn noflap=web,cpu
EOF

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/xymond-noflap-list-harness.c" "$ROOT/lib/libxymoncomm.a" \
	-lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" "$work/hosts.cfg" 2>"$work/stderr.log" \
	|| fail "noflap assertions failed:
$(cat "$work/stderr.log")"

pass "noflap= list keeps working across repeated evaluations and leaves the host record intact"
