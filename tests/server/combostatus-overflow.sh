#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/combostatus-overflow.sh
#
# Regression guard for the stack buffer overflow in combostatus evaluate()
# (issue #187, commit 6252bd9ca).
#
# evaluate() formatted a compute() error into a fixed errtext[1024] stack
# buffer with an unbounded sprintf(), while the expression it interpolates
# (expr) can be up to MAX_LINE_LEN (16 KB). A combo expression longer than
# ~990 chars that makes compute() fail overran the stack. The fix bounds the
# write with snprintf(errtext, sizeof(errtext), ...).
#
# evaluate() is static and pulls in compute() and the value machinery, so --
# like tests/server/digest-md5hash.sh -- this (1) binds to the real source:
# the bounded snprintf must be present and the unbounded sprintf into errtext
# gone, and (2) compiles a faithful copy of the format step and shows that a
# 16 KB expression is written into the 1 KB buffer without touching a trailing
# canary. The unbounded form is deliberately NOT executed (it is the overflow
# under test); the demo asserts the fixed form's safety property instead.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/xymond/combostatus.c"
CC=${CC:-cc}

[ -f "$SRC" ] || skip "xymond/combostatus.c absent"

# (1) Bind to the real code. The fixed line is
#   snprintf(errtext, sizeof(errtext), "compute(%s)...
# the bug was an unbounded
#   sprintf(errtext, "compute(%s)...
# "sprintf(errtext" is a substring of "snprintf(errtext", so match the buggy
# form as sprintf NOT preceded by an 'n' -- that fires only on the real bug.
assert_contains "snprintf(errtext, sizeof(errtext)," "$(cat "$SRC")" \
	"combostatus.c evaluate() lost the bounded snprintf for the compute() error (#187 / 6252bd9ca)"
grep -Eq '(^|[^n])sprintf\(errtext,' "$SRC" \
	&& fail "combostatus.c evaluate() regressed to an unbounded sprintf into errtext[1024] (#187 / 6252bd9ca)"

# (2) Behavioural demo of the property, if we can compile.
command -v "$CC" >/dev/null 2>&1 \
	|| pass "combostatus.c keeps the #187 fix (static check; no C compiler for the run)"

work=$(mktempdir)
cat >"$work/t.c" <<'EOF'
#include <stdio.h>
#include <string.h>

#define MAX_LINE_LEN 16384

int main(void)
{
	/* Mirror evaluate()'s stack layout: the 1 KB target plus a canary right
	 * after it, so an overrun of errtext would be observable. */
	struct { char errtext[1024]; char canary; } s;
	char expr[MAX_LINE_LEN];
	int written;

	s.canary = '#';
	memset(expr, 'A', sizeof(expr) - 1);
	expr[sizeof(expr) - 1] = '\0';   /* a full 16 KB expression, as compute() may see */

	/* The fixed format step: bounded by the real buffer size. */
	written = snprintf(s.errtext, sizeof(s.errtext), "compute(%s) returned error %d\n", expr, 7);

	/* snprintf must not have written past the buffer... */
	if (strlen(s.errtext) >= sizeof(s.errtext)) {
		fprintf(stderr, "errtext not NUL-bounded: len=%zu\n", strlen(s.errtext));
		return 1;
	}
	/* ...the canary just past it must be intact... */
	if (s.canary != '#') {
		fprintf(stderr, "canary clobbered -- errtext overran\n");
		return 1;
	}
	/* ...and snprintf must report the truncation (return >= buffer size), which
	 * is exactly the signal the unbounded sprintf ignored while overrunning. */
	if (written < (int)sizeof(s.errtext)) {
		fprintf(stderr, "expected truncation (written=%d >= %zu)\n", written, sizeof(s.errtext));
		return 1;
	}
	return 0;
}
EOF

"$CC" -std=c99 -Wall -Wextra -Werror -o "$work/t" "$work/t.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "combostatus overflow probe did not compile"; }
"$work/t" || fail "combostatus overflow probe failed: the bounded-format property broke (#187)"

pass "combostatus evaluate() bounds a 16 KB expression into errtext[1024] without overrunning (#187)"
