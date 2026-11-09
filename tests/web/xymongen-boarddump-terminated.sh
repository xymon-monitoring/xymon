#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xymongen-boarddump-terminated.sh
#
# xymongen reads a status board from the file BOARDDUMP names
# (xymongen/loaddata.c, load_state) into one string, and the parsing after
# it runs to the string's end. The read left that end unterminated: the
# buffer had room for the '\0' but nothing wrote it, so the string ran on
# into whatever followed in memory, and only luck -- memory that happened to
# be zero -- stopped it.
#
# AddressSanitizer takes the luck away: it fills a new allocation with 0xbe,
# so an unterminated board is followed by a non-zero byte and then the
# redzone, and the read past the end is reported every time. The test builds
# xymongen from its sources with -fsanitize=address, gives it a two-line
# board, and checks that it runs without a report. Skipped where the
# compiler has no AddressSanitizer.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_cc
asan_usable || skip "the compiler has no AddressSanitizer"
ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a libxymontime.a

# The libraries the xymongen Makefile links, scraped as svcstatus-cgi.sh does.
pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")

src="$ROOT/xymongen"
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" -fsanitize=address -g $cflags -iquote "$src" -o "$work/xymongen" \
	"$src/xymongen.c" "$src/loadlayout.c" "$src/loaddata.c" "$src/pagegen.c" \
	"$src/process.c" "$src/wmlgen.c" "$src/rssgen.c" "$src/util.c" \
	"$src/debug.c" "$src/csvreport.c" \
	"$(xymon_lib "$ROOT" libxymontime.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "xymongen does not build with AddressSanitizer"; }

mkdir -p "$work/home/etc" "$work/home/tmp" "$work/www/notes" \
	"$work/www/help" "$work/acks" "$work/logs" "$work/hist"
cp -R "$ROOT/xymond/webfiles" "$work/home/web"
printf 'page main Main\n127.0.0.1 alpha.test # conn\n' > "$work/home/etc/hosts.cfg"
# hostname|testname|color|flags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|line1|acklist
cat > "$work/board" <<'BOARD'
alpha.test|info|green||0|0|0|0|0||||
alpha.test|conn|red||1791500883|1791500883|1791502683|0|0|127.0.0.1|819012|red alpha conn down|
BOARD

# The fill is AddressSanitizer's default; it is set here so the test does
# not depend on it. Leaks are not what this test is about.
rc=0
(cd "$work" && env -i PATH="$PATH" \
	ASAN_OPTIONS=detect_leaks=0:malloc_fill_byte=190:max_malloc_fill_size=4096 \
	XYMONHOME="$work/home" HOSTSCFG="$work/home/etc/hosts.cfg" \
	XYMONTMP="$work/home/tmp" XYMONWWWDIR="$work/www" \
	XYMONACKDIR="$work/acks" XYMONSERVERLOGS="$work/logs" \
	XYMONHISTDIR="$work/hist" BOARDDUMP="$work/board" \
	"$work/xymongen" "$work/www") > "$work/xymongen.out" 2> "$work/xymongen.err" || rc=$?

if grep -q "ERROR: AddressSanitizer" "$work/xymongen.err"; then
	sed -n '1,12p' "$work/xymongen.err" >&2
	fail "xymongen read past the end of the board it loaded from BOARDDUMP"
fi
assert_equal "0" "$rc" "xymongen exited with status $rc: $(tail -c 300 "$work/xymongen.err")"
assert_file_exists "$work/www/nongreen.html" "xymongen wrote no non-green page"

pass "xymongen reads a board from BOARDDUMP as a terminated string"
