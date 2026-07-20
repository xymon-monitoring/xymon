#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/testcfg-rrdmap.sh
#
# test.cfg overlays the column->RRD mapping that TEST2RRD provides: a
# single-metric TEST binds its column to that metric and adds new columns,
# but an IMPLICIT binding never overrides a conflicting env mapping (that
# takes an explicit HANDLER); columns with no section fall back to the
# TEST2RRD environment. Drives the real find_xymon_rrd().

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-tcrrd.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/testcfg-rrdmap-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

mkdir -p "$work/etc"
cat >"$work/etc/test.cfg" <<'EOF'
TEST cpu       { SOURCE client; METRIC cpu2 }
TEST vmtemp    { SOURCE client; METRIC vm_thermal; HANDLER vm_thermal }
TEST diskquick { SOURCE script; METRIC diskfam }
TEST diskio {
        SOURCE script
        METRIC diskio_ops  { LAZY }
        METRIC diskio_busy
}
EOF

XYMONHOME="$work" \
TEST2RRD="cpu=la,http=tcp,disk,vmtemp=ncv" \
GRAPHS="la,disk" \
	"$work/harness" 2>"$work/stderr.log" || fail "rrdmap assertions failed: $(cat "$work/stderr.log")"

pass "test.cfg overlays column->RRD mapping over TEST2RRD, with env fallback"
