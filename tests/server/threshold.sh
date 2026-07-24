#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/threshold.sh
#
# threshold_eval: evaluate a metric value against a declared THRESHOLD spec
# ("<ds>:<relop><operand>[:<sev>][,...]") to the worst firing severity - the
# shared engine used by the status table and (RFC #218) the alert path. The
# function is self-contained, so this compiles it directly - no built tree
# required. See the harness for the full assertion list.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-threshold.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$CC" -I"$ROOT/lib" -o "$work/harness" \
	"$here/threshold-harness.c" "$ROOT/lib/threshold.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" 2>"$work/stderr.log" \
	|| fail "harness assertions failed: $(cat "$work/stderr.log")"

pass "threshold_eval maps value + spec to the worst firing severity"
