#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/aggregate-tokens-values.sh
#
# What an aggregate token computes, not only what it expands to: each
# token's expansion, printed by aggregate-tokens-harness.c from the same
# source showgraph.c compiles, is evaluated by rrdtool over three files
# with known values, and the result is compared with the expected number.
# aggregate-tokens.sh pins the expansions; this pins their meaning, which
# is where RRDtool's own rules (unknown values, percentile ranks) bite.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=tests/lib/assert.sh
. "$here/../lib/assert.sh"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v rrdtool >/dev/null 2>&1 || skip "no rrdtool command to evaluate the expansions with"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

harness="$work/aggregate-tokens"
"$CC" -g -O1 -iquote "$here/../../web" -o "$harness" "$here/aggregate-tokens-harness.c" \
	2>"$work/cc.log" || { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# One RRD gives the time axis; the "files" are constant CDEFs over it.
# k: 10,20,30 (known)  m: 10,20,unknown (mixed)  u: all unknown
# dk1..dk3: 10,20,30 as datasets of one file, for @DSMEDIAN:
T=1000000000
rrd="$work/x.rrd"
rrdtool create "$rrd" --start "$T" --step 60 DS:v:GAUGE:120:U:U RRA:AVERAGE:0.5:1:100
i=1
while [ "$i" -le 20 ]; do
	rrdtool update "$rrd" "$((T + 60 * i)):1"
	i=$((i + 1))
done
defs=("DEF:x=$rrd:v:AVERAGE")
for def in k0:10 k1:20 k2:30 m0:10 m1:20 dk1:10 dk2:20 dk3:30; do
	defs+=("CDEF:${def%%:*}=x,0,*,${def#*:},+")
done
for var in m2 u0 u1 u2; do
	defs+=("CDEF:$var=x,UNKN,+")
done

failures=0
check() {	# token files want
	local rpn got
	rpn=$("$harness" "$1" "$2")
	got=$(rrdtool graph /dev/null --start "$((T + 300))" --end "$((T + 900))" \
		"${defs[@]}" "CDEF:r=$rpn" 'PRINT:r:LAST:%.4lf' 2>&1 | tail -n 1) || true
	case "$got" in *nan*|*NaN*) got=nan ;; esac
	if [ "$got" != "$3" ]; then
		echo "FAIL $1 over $2: $rpn -> $got, want $3" >&2
		failures=$((failures + 1))
	fi
}
check '@AVG:m@'       3 nan		# one unknown file makes the strict forms unknown
check '@SUM:m@'       3 nan
check '@MIN:m@'       3 nan
check '@MAX:k@'       3 30.0000
check '@AVGNAN:m@'    3 15.0000
check '@SUMNAN:m@'    3 30.0000
check '@MINNAN:m@'    3 10.0000
check '@MAXNAN:m@'    3 20.0000
check '@COUNT:m@'     3 2.0000
check '@COUNT:u@'     3 0.0000
check '@MEDIAN:k@'    3 20.0000
check '@MEDIAN:m@'    3 15.0000		# unknowns skipped; even count averages the middle two
check '@STDEV:k@'     3 10.0000		# sample deviation: divides by n-1
check '@STDEV:k@'     1 nan			# undefined for one value
check '@P0:k@'        3 10.0000		# below 50/n: the smallest value
check '@P10:k@'       3 10.0000
check '@P50:k@'       3 20.0000
check '@P95:k@'       3 30.0000
check '@P100:k@'      3 30.0000
check '@P50:m@'       3 10.0000		# PERCENT counts an unknown as the lowest value
check '@PERCENT:k:50@' 3 20.0000
check '@P150:k@'      3 nan			# out of range: unknown, not a failed graph
check '@PERCENT:k:abc@' 3 nan		# malformed: likewise
check '@DSMEDIAN:dk@' 3 20.0000

[ "$failures" = 0 ] || fail "$failures aggregate token(s) compute the wrong value (see above)"
pass "aggregate tokens compute the expected values in rrdtool $(rrdtool --version | awk 'NR == 1 { print $2 }')"
