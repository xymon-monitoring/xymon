#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/xymonping-align.sh
#
# Pins that xymonping reads and writes its ping payload without a misaligned
# access: xymonping-align-harness.c sends a ping and parses a reply through
# the real xymonnet/xymonping.c, for an IPv4 and an IPv6 host, built with
# UBSan's alignment check made fatal. On x86 a misaligned access is harmless,
# which is why it went unnoticed; on a strict-alignment CPU (SPARC) it faults.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(cd "$(dirname "$0")" && pwd)

require_bin XYMONPING xymonnet/xymonping
require_c_buildenv "$ROOT"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymontime.a libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
san="-fsanitize=alignment -fno-sanitize-recover=alignment"
printf 'int main(void) { return 0; }\n' > "$work/probe.c"
# shellcheck disable=SC2086  # the flag list is a word list
"$CC" $san -o "$work/probe" "$work/probe.c" 2>/dev/null \
	|| skip "the compiler has no UBSan alignment check ($CC $san)"
# The probe must also run: a compiler can link the check against a
# libubsan the system never installed (Amazon Linux 2023, Solaris 11.4),
# and the harness would then die at load time, not at the check.
"$work/probe" 2>/dev/null \
	|| skip "the UBSan runtime is not installed: a program built with $san does not start"
# shellcheck disable=SC2086  # the flag lists are word lists
"$CC" -g -O1 $san $harness_cflags -iquote "$ROOT/xymonnet" -o "$work/harness" \
	"$here/xymonping-align-harness.c" "$ROOT/lib/libxymontime.a" "$ROOT/lib/libxymoncomm.a" \
	$harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" >"$work/run.log" 2>&1 \
	|| { cat "$work/run.log" >&2; fail "xymonping accesses its payload misaligned, or lost the reply (see above)"; }

pass "xymonping writes and reads its ping payload without a misaligned access"
