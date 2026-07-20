#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-exec-title.sh
#
# A graphs.cfg "TITLE exec:<cmd>" runs <cmd> with the graph's displayname,
# service and matched RRD filenames as arguments, and uses its first line
# of output as the graph title. The displayname comes from the "disp="
# CGI parameter, so those arguments must be passed to the script as
# literal words, never interpreted by the shell. This test drives the
# real showgraph CGI with values containing shell-active characters and
# asserts they reach the script verbatim, with no shell evaluation.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"
[ -f "$ROOT/web/showgraph.cgi" ] || skip "tree built without RRD support (no showgraph.cgi)"

rrddef=$(sed -n 's/^RRDDEF *= *//p' "$ROOT/Makefile")
rrdlibs=$(sed -n 's/^RRDLIBS *= *//p' "$ROOT/Makefile")
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-showgraph-exec.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" $rrddef -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $rrdlibs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

rrds="$work/rrd/testhost"
mkdir -p "$rrds"
touch "$rrds/tcp.conn.rrd"

# The exec title script: record every argument it is handed (one per
# line), and echo a fixed first line as the title.
cat >"$work/titlescript.sh" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" >"$TITLE_ARGS_OUT"
echo "the title"
EOF
chmod +x "$work/titlescript.sh"

cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"
cat >>"$work/graphs.cfg" <<EOF

[exectitle]
	FNPATTERN ^tcp\.conn\.rrd
	TITLE exec:$work/titlescript.sh
	YAXIS x
	DEF:p@RRDIDX@=@RRDFN@:sec:AVERAGE
	LINE2:p@RRDIDX@#@COLOR@:x
EOF

render() {  # render <disp-value>
	rm -f "$work/SIDEEFFECT" "$work/args.out"
	REQUEST_METHOD=GET \
	QUERY_STRING="host=testhost&service=exectitle&graph=hourly&action=view&disp=$1" \
	XYMONHOME="$work" TITLE_ARGS_OUT="$work/args.out" \
		"$work/showgraph" --debug --config="$work/graphs.cfg" \
		--rrddir="$rrds" >/dev/null 2>&1 || true
}

# A benign displayname is passed through verbatim as the first argument.
render "myhost"
[ -f "$work/args.out" ] || fail "exec title script did not run"
grep -qx "myhost" "$work/args.out" || fail "benign displayname not passed literally: $(cat "$work/args.out")"

# A "$(...)" value must be passed literally, not command-substituted:
# the side-effect file must not appear, and the value must reach the
# script as one argument, verbatim.
val='x$(touch '"$work"'/SIDEEFFECT)y'
render "$val"
[ -e "$work/SIDEEFFECT" ] && fail "exec-title argument was shell-evaluated (\$() ran)"
grep -qx "$val" "$work/args.out" \
	|| fail "value with shell characters not passed as one literal argument: $(cat "$work/args.out")"

# A value containing a double quote must not terminate an argument early.
val2='a";touch '"$work"'/SIDEEFFECT;echo "b'
render "$val2"
[ -e "$work/SIDEEFFECT" ] && fail "exec-title argument was shell-evaluated (double quote)"
grep -qx "$val2" "$work/args.out" || fail "value with a double quote not literal: $(cat "$work/args.out")"

# A value containing backticks must not be command-substituted.
val3='p`touch '"$work"'/SIDEEFFECT`q'
render "$val3"
[ -e "$work/SIDEEFFECT" ] && fail "exec-title argument was shell-evaluated (backticks)"
grep -qx "$val3" "$work/args.out" || fail "value with backticks not literal: $(cat "$work/args.out")"

echo "OK $(basename "$0")"
