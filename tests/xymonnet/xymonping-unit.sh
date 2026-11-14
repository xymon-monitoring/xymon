#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/xymonping-unit.sh
#
# Compiles xymonping-harness.c, which #includes the real xymonnet/xymonping.c
# and feeds its reply parser crafted packets through a socketpair, and fails if
# any rule it pins does not hold. See the harness for the list. Needs the tree
# built, for the libraries xymonping links.

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
# Sanitizers, where the compiler has them, with UBSan made fatal: its
# alignment check pins that the payload is never accessed through a
# misaligned pointer. ASan comes along where it runs (asan_usable); without
# it, the alignment check alone where the compiler has it.
san="-g -O1"
printf 'int main(void) { return 0; }\n' > "$work/probe.c"
if asan_usable; then
	san="$san -fsanitize=address,undefined -fno-sanitize-recover=undefined"
elif "$CC" -fsanitize=alignment -fno-sanitize-recover=alignment -o "$work/probe" "$work/probe.c" 2>/dev/null \
		&& "$work/probe" 2>/dev/null; then
	san="$san -fsanitize=alignment -fno-sanitize-recover=alignment"
fi
# shellcheck disable=SC2086  # the flag lists are word lists
"$CC" $san $harness_cflags -iquote "$ROOT/xymonnet" -o "$work/harness" "$here/xymonping-harness.c" \
	"$(xymon_lib "$ROOT" libxymontime.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" "$work/harness" >"$work/run.log" 2>&1 \
	|| { cat "$work/run.log" >&2; fail "xymonping's reply parser breaks a rule its harness pins (see above)"; }

pass "xymonping's reply parser holds every rule its harness pins, on raw and unprivileged sockets"
