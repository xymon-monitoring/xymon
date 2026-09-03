#!/usr/bin/env bash
# Every entry in protocols.cfg is run by the dialogue driver.
#
# Two matchers behaving almost alike is the problem: they agree on every
# ordinary reply and part company only when one arrives split across
# segments, where the older arm judges the first read alone and calls a
# healthy server down. Which arm an entry lands on is decided by the shape
# of its steps, so an edit to an entry could move it silently.
#
# The older arm stays for the compiled-in http/https tests, which never come
# from this file. Nothing configured here should reach it.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

require_cc
WORK=$(mktempdir); register_cleanup "rm -rf '$WORK'"

[ -f "$ROOT/include/config.h" ] || skip "tree is not configured"

build_xymon_libs "$ROOT" "$WORK/libbuild.log" libxymoncomm.a
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# shellcheck disable=SC2086
"$CC" $harness_cflags -o "$WORK/harness" \
	"$(dirname "$0")/protocols-driver.c" \
	"$ROOT/lib/libxymoncomm.a" $pcre_libs $harness_ldflags 2>"$WORK/cc.log" \
	|| { cat "$WORK/cc.log" >&2; fail "the harness does not compile"; }

mkdir -p "$WORK/etc"
cp "$ROOT/xymonnet/protocols.cfg" "$WORK/etc/protocols.cfg"

# One name per entry: the first alias of each header is enough, since the
# aliases of a header are copies of the same record.
names=$(sed -n 's/^\[\([^]|]*\).*\]$/\1/p' "$WORK/etc/protocols.cfg")
[ -n "$names" ] || fail "no service names found in protocols.cfg"

# shellcheck disable=SC2086
XYMONHOME="$WORK" "$WORK/harness" $names > "$WORK/out" 2>"$WORK/err" \
	|| { cat "$WORK/err" >&2; fail "the harness did not run"; }

if grep -q 'LEGACY' "$WORK/out"; then
	fail "these entries are still run by the older send-and-match arm. It judges
the first read on its own, so a reply split across two segments is read as a
short reply and a healthy server is reported down:
$(grep 'LEGACY' "$WORK/out" | sed 's/^/  /')"
fi

count=$(grep -c 'driver' "$WORK/out")
[ "$count" -ge 30 ] || fail \
	"only $count entries were checked; the name list looks wrong, and a test
that checks almost nothing passes just as green as one that checks it all:
$(head -5 "$WORK/out")"

pass "all $count entries in protocols.cfg are run by the dialogue driver"
