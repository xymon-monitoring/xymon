#!/usr/bin/env bash
# A parsed send, expect or until must be as long as the parser says it is.
#
# getescapestring() resolves \xNN, so any of them may contain a NUL. Copying
# one with strdup() while carrying its length separately gives an allocation
# that stops at the NUL and a length that does not -- and contest.c then
# writes, or matches, the length.
#
# Found three times in three places by reading the code, which is a poor way
# to find the fourth. Here the parser runs for real and every buffer it made
# is read back by its own recorded length under ASan.
#
# The escapes are on an alias header and the LAST alias is checked: services
# after the first get their own copies, and those were the wrong ones.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

require_cc
WORK=$(mktempdir); register_cleanup "rm -rf '$WORK'"

# Probed on its own: folding it into the harness build would report any
# compile error at all as "no ASan" and pass while checking nothing.
asan_usable || skip "$CC cannot build and run ASan binaries"

[ -f "$ROOT/include/config.h" ] || skip "tree is not configured"

build_xymon_libs "$ROOT" "$WORK/libbuild.log" libxymoncomm.a
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# shellcheck disable=SC2086
"$CC" $harness_cflags -fsanitize=address -o "$WORK/harness" \
	"$(dirname "$0")/protocols-escaped-lengths.c" \
	"$ROOT/lib/libxymoncomm.a" $pcre_libs $harness_ldflags 2>"$WORK/cc.log" \
	|| { cat "$WORK/cc.log" >&2; fail "the harness does not compile"; }

mkdir -p "$WORK/etc"

# A NUL in each of the three, on an alias header so the copies are exercised.
cat > "$WORK/etc/protocols.cfg" <<'CFG'
[aaa|bbb|ccc]
   send "\x00AAAA\r\n"
   expect "\x00OK"
   expect "250" until "\x00250 "
   port 1
CFG

run_harness() {
	env XYMONHOME="$WORK" ASAN_OPTIONS=detect_leaks=0 "$WORK/harness" "$@" 2>&1
}

# ccc is the last alias, so it is the furthest from the entry the text was
# parsed into -- and it is the one whose copies were made with strdup().
out=$(run_harness aaa ccc) || {
	printf '%s\n' "$out" | grep -qE 'AddressSanitizer|runtime error' && fail \
"a parsed buffer is shorter than the length recorded with it. Reading it by
that length is an overflow here; in xymonnet it is what goes out on the
socket, or what an expect is matched against:
$(printf '%s\n' "$out" | grep -E 'ERROR|SUMMARY|#[0-9]' | head -12)"
	fail "the harness failed without a sanitizer report:
$out"
}

# And prove the harness would have said so. Same parser, same reader -- only
# the shipped file, which must define none of this and still come out whole.
cp "$ROOT/xymonnet/protocols.cfg" "$WORK/etc/protocols.cfg"
shipped=$(sed -n 's/^\[\([a-z0-9]*\)[]|].*/\1/p' "$ROOT/xymonnet/protocols.cfg" | tr '\n' ' ')
[ -n "$shipped" ] || fail "could not read the service names out of the shipped protocols.cfg"

# shellcheck disable=SC2086
out=$(run_harness $shipped) || fail \
"the shipped protocols.cfg produced a buffer shorter than its recorded length:
$(printf '%s\n' "$out" | grep -E 'ERROR|SUMMARY|#[0-9]' | head -12)"

pass "every parsed send, expect and until is as long as the length recorded with it, aliases included"
