#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-index-width.sh
#
# Regression guard for the stack buffer overrun in msgcache's pull index
# (client/msgcache.c, grabdata()).
#
# Each queued message gets an index entry "<length>:<age> ", formatted with
# an unbounded sprintf() into char idx[20]. The age is now - tstamp, so a
# clock stepped back after a message was queued makes it a large negative
# number: a 7-digit length and an age of about -1.7e9 (a clock reset to the
# epoch) already need 21 bytes, and the widest entry, INT_MIN:LONG_MIN on an
# LP64 host, needs 34. The fix formats it with snprintf() into idx[64].
#
# msgcache takes its time from the system clock and has no option to fake
# it, so the overrun cannot be reached from a running daemon here. Like
# tests/server/combostatus-overflow.sh, this (1) binds to the real source:
# the bounded snprintf into idx[64] must be present and the unbounded
# sprintf into idx gone, and (2) compiles a faithful copy of the format step
# and shows the widest entry fits idx[64] without touching a trailing canary,
# and would not have fit the old idx[20]. The unbounded form is deliberately
# NOT executed (it is the overrun under test).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/client/msgcache.c"
CC=${CC:-cc}

[ -f "$SRC" ] || skip "client/msgcache.c absent"

# (1) Bind to the real code. "sprintf(idx," is a substring of
# "snprintf(idx,", so match the buggy form as sprintf NOT preceded by an
# 'n' -- that fires only on the real bug.
assert_contains "char idx[64];" "$(cat "$SRC")" \
	"msgcache.c grabdata() no longer sizes the pull index entry at 64 bytes"
assert_contains 'snprintf(idx, sizeof(idx), "%d:%ld ",' "$(cat "$SRC")" \
	"msgcache.c grabdata() lost the bounded snprintf for the pull index entry"
grep -Eq '(^|[^n])sprintf\(idx,' "$SRC" \
	&& fail "msgcache.c grabdata() regressed to an unbounded sprintf into idx"

# (2) Behavioural demo of the property, if we can compile.
command -v "$CC" >/dev/null 2>&1 \
	|| pass "msgcache.c bounds the pull index entry (static check; no C compiler for the run)"

work=$(mktempdir)
cat >"$work/t.c" <<'EOF'
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	/* Mirror grabdata()'s buffer plus a canary right after it, so an
	 * overrun of idx would be observable. */
	struct { char idx[64]; char canary; } s;
	int written;

	s.canary = '#';

	/* The fixed format step, fed the widest values the format can take. */
	written = snprintf(s.idx, sizeof(s.idx), "%d:%ld ", INT_MIN, LONG_MIN);

	/* The widest entry must fit whole -- truncating it would corrupt the
	 * index line the server parses... */
	if (written < 0 || written >= (int)sizeof(s.idx)) {
		fprintf(stderr, "widest entry truncated: written=%d, buffer %zu\n",
			written, sizeof(s.idx));
		return 1;
	}
	/* ...the canary just past the buffer must be intact... */
	if (s.canary != '#') {
		fprintf(stderr, "canary clobbered -- idx overran\n");
		return 1;
	}
	/* ...and the old 20-byte buffer could not have held it, which is the
	 * overrun the unbounded sprintf committed. */
	if (written + 1 <= 20) {
		fprintf(stderr, "widest entry is only %d bytes; idx[20] would have held it\n",
			written + 1);
		return 1;
	}
	return 0;
}
EOF

"$CC" -std=c99 -Wall -Wextra -Werror -o "$work/t" "$work/t.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "msgcache index-width probe did not compile"; }
"$work/t" || fail "msgcache index-width probe failed: the widest pull index entry does not fit idx[64]"

pass "msgcache grabdata() formats the widest pull index entry into idx[64] without overrunning"
