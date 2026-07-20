#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/rrdinstance.sh
#
# Reversible, collision-free RRD instance encoding. A metrics-block instance
# (a mount point, a name with a comma or a space) must round-trip to exactly
# one unambiguous "<base>.<instance>.rrd" file and back to the original name.
# The encoder is self-contained, so this compiles it directly - no built tree
# required. See the harness for the full assertion list.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-rrdinstance.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$CC" -I"$ROOT/lib" -o "$work/harness" \
	"$here/rrdinstance-harness.c" "$ROOT/lib/rrdinstance.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" 2>"$work/stderr.log" \
	|| fail "harness assertions failed: $(cat "$work/stderr.log")"

pass "RRD instance encoding round-trips and is collision-free"
