#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/csvinfo-bounds.sh
#
# csvinfo.cgi shows one row of a CSV database in $XYMONHOME/etc/, chosen by
# the request: the file (db=), the delimiter and the key column (column=).
# Each is now bounded:
#
# - db= must name a .csv file;
# - column= must be a column csvinfo keeps (0 to 79), written as a number;
# - the delimiter must not be empty;
# - a line with more columns than csvinfo keeps, or a field longer than a
#   line buffer, is cut to fit.
#
# Built from its source with AddressSanitizer where the compiler has it, so
# an out-of-range access is reported rather than left to chance; with the
# built CGI otherwise, and the test then reports that part as not checked.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)

if asan_usable && [ -f "$ROOT/include/config.h" ] && [ -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ]; then
	require_cc
	build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a
	pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
	[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
	cflags=$(xymon_cflags "$ROOT")
	ldflags=$(xymon_ldflags "$ROOT")
	# shellcheck disable=SC2086  # the flag lists are meant to split
	"$CC" -fsanitize=address,undefined -g $cflags -o "$work/csvinfo" "$ROOT/web/csvinfo.c" \
		"$(xymon_lib "$ROOT" libxymoncomm.a)" $pcre_libs $ldflags 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "csvinfo does not build with AddressSanitizer"; }
	CSVINFO="$work/csvinfo"
	asan=yes
else
	require_bin CSVINFO web/csvinfo.cgi
	asan=no
fi

mkdir -p "$work/etc"
printf 'host;owner;room;\nalpha;ops;r1;\nbeta;dev;r2;\n' > "$work/etc/hostinfo.csv"
printf 'host;owner;\ngamma;net;\n' > "$work/etc/other.csv"

# A database 100 columns wide, and one with a field longer than a line buffer
{
	for i in $(seq 1 100); do printf 'h%s;' "$i"; done; printf '\n'
	printf 'wide;'; for i in $(seq 2 100); do printf 'v%s;' "$i"; done; printf '\n'
} > "$work/etc/wide.csv"
long=$(printf '%20000s' '' | tr ' ' x)
printf 'host;value;\nlong;%s;\n' "$long" > "$work/etc/long.csv"

# render QUERY -- the page, and RC; a run that dies on a signal or that
# AddressSanitizer reports on fails the test here
render() {
	RC=0
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="$1" XYMONHOME="$work" \
		HTMLCONTENTTYPE="text/html" ASAN_OPTIONS=detect_leaks=0 \
		"$CSVINFO" 2>"$work/err") || RC=$?
	if [ "$RC" -gt 128 ] || grep -q 'AddressSanitizer' "$work/err"; then
		sed -n '1,8p' "$work/err" >&2
		fail "csvinfo did not survive $1 (status $RC)"
	fi
}

# What csvinfo is for still works
render "key=beta"
assert_contains "dev" "$OUT" "the row of hostinfo.csv for beta is shown"
render "db=other.csv&key=gamma"
assert_contains "net" "$OUT" "another .csv database can be named"
render "db=hostinfo.csv&key=r1&column=2"
assert_contains "alpha" "$OUT" "a row is found by another column"

# db=: only a .csv name
for db in hostinfo.cfg notes.txt .csv plain; do
	render "db=$db&key=beta"
	assert_contains "Invalid database" "$OUT" "db=$db is refused"
done

# column=: only the columns csvinfo keeps
for c in -1 80 1000 abc 3x 99999999999999999999; do
	render "key=beta&column=$c"
	assert_contains "Invalid column" "$OUT" "column=$c is refused"
done
# delimiter=: an empty value is refused
render "key=beta&delimiter="
assert_contains "Invalid delimiter" "$OUT" "an empty delimiter= is refused"
render "key=beta&column="
assert_contains "dev" "$OUT" "an empty column= is the default column"
render "key=beta&column=79"
assert_not_contains "Invalid column" "$OUT" "column=79 is accepted"

# A line wider than csvinfo's columns, and a field longer than its buffer
render "db=wide.csv&key=wide"
assert_equal "0" "$RC" "a line of 100 columns is shown"
assert_contains ">h80<" "$OUT" "all 80 columns csvinfo keeps are shown"
assert_not_contains ">h81<" "$OUT" "the columns past them are left out"
render "db=long.csv&key=long"
assert_equal "0" "$RC" "a field longer than a line buffer is shown"
assert_not_contains "$long" "$OUT" "a field longer than a line buffer is cut"

claim="csvinfo shows only .csv databases, only the columns it keeps, and cuts what does not fit"
if [ "$asan" = yes ]; then
	pass "$claim"
else
	pass_partial "$claim" "no AddressSanitizer here: the built CGI was run, so an out-of-range access may go unseen"
fi
