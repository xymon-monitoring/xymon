#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/aggds-rules.sh
#
# AGGDS aggregate rules in analysis.cfg: sum/avg/max/min/count over the
# latest stored values of a metric set, with a freshness window and
# first-match shadowing. Drives the real parser, store and checker from
# client_config.c through a small C harness.

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-aggds.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -I"$ROOT/xymond" -o "$work/harness" \
	"$here/aggds-rules-harness.c" "$ROOT/xymond/client_config.c" \
	"$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

mkdir -p "$work/etc"
cat >"$work/etc/hosts.cfg" <<'EOF'
0.0.0.0 testhost # linux
0.0.0.0 otherhost # linux
0.0.0.0 scopedhost # linux
EOF
cat >"$work/etc/analysis.cfg" <<'EOF'
HOST=*
	AGGDS diskio sum(%diskio_ops\..+\.rrd:reads) >100 COLOR=yellow "TEXT=Total reads high: &V"
	AGGDS diskio sum(%diskio_ops\..+\.rrd:reads) >1 COLOR=yellow "TEXT=SHADOWED"
	AGGDS diskio max(%diskio_ops\..+\.rrd:reads) >100 COLOR=yellow "TEXT=Max read &V"
	AGGDS diskio count(%diskio_ops\..+\.rrd:reads) <3 COLOR=red "TEXT=Disks missing: only &V reporting"
	AGGDS diskio2 max(%diskio_ops\..+\.rrd:writes) >250 COLOR=yellow
	AGGDS diskio3 avg(%diskio_ops\..+\.rrd:reads) >100 COLOR=yellow "TEXT=avg &V"
	AGGDS diskio4 sum(%diskio_ops\.ada.+:reads) >10 COLOR=yellow "TEXT=ada sum &V"
	AGGDS diskio4 sum(%diskio_ops\.da.+:reads) >10 COLOR=yellow "TEXT=da sum &V"
	AGGDS diskio5 count(%diskio_ops\..+\.rrd:reads) <10 COLOR=red "TEXT=crit few"
	AGGDS diskio5 count(%diskio_ops\..+\.rrd:reads) <20 COLOR=yellow "TEXT=warn few"
	AGGDS diskio6 sum(%diskio_ops\..+\.rrd:reads) >notanumber COLOR=red "TEXT=garbage limit fired"
HOST=scopedhost
	AGGDS broken sum(no-dataset) >1 COLOR=red
	AGGDS scoped sum(%diskio_ops\..+\.rrd:reads) >1 COLOR=yellow "TEXT=scoped fired: &V"
EOF
: >"$work/etc/empty-analysis.cfg"

XYMONHOME="$work" \
HOSTSCFG="$work/etc/hosts.cfg" \
EMPTYCFG="$work/etc/empty-analysis.cfg" \
MACHINE="testhost" \
	"$work/harness" 2>"$work/stderr.log" || fail "harness assertions failed: $(cat "$work/stderr.log")"

pass "AGGDS rules aggregate fresh metric sets with first-match shadowing"
