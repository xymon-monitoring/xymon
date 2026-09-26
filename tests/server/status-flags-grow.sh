#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/status-flags-grow.sh
#
# Regression guard for the one-byte heap overflow in handle_status() where the
# test flags are grown.
#
# When a status message's "<!-- [flags:...] -->" string is longer than the
# flags already stored for that host.test, handle_status() reallocated the
# buffer to strlen(flagstart) and then strcpy()'d strlen(flagstart)+1 bytes
# into it -- the terminating NUL landed one byte past the allocation. Reachable
# from anything that can send xymond a status message: send one short flags
# string, then a longer one for the same host.test.
#
# handle_status() is static and pulls in the whole daemon, so -- like
# tests/server/combostatus-overflow.sh -- this (1) binds to the real source:
# the grow-branch realloc must size strlen()+1 and the short realloc must be
# gone, and (2) compiles a faithful copy of the realloc+strcpy step with a
# canary right after the allocation and shows the string, plus its NUL, fits.
# The short-by-one form is deliberately NOT executed (it is the overflow under
# test); the demo asserts the fixed form's safety property instead.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/xymond/xymond.c"
CC=${CC:-cc}

[ -f "$SRC" ] || skip "xymond/xymond.c absent"

# (1) Bind to the real code. The fixed grow-branch is
#   log->testflags = realloc(log->testflags, strlen(flagstart) + 1);
# the bug sized it to strlen(flagstart) with no room for the NUL strcpy writes.
assert_contains "realloc(log->testflags, strlen(flagstart) + 1)" "$(cat "$SRC")" \
	"xymond.c handle_status() lost the +1 sizing the grown testflags buffer for its NUL"
grep -Eq 'realloc\(log->testflags, strlen\(flagstart\)\)[^+]' "$SRC" \
	&& fail "xymond.c handle_status() regressed to realloc(strlen(flagstart)) -- strcpy overruns by one"

# (2) Behavioural demo of the property, if we can compile.
command -v "$CC" >/dev/null 2>&1 \
	|| pass "xymond.c keeps the testflags +1 sizing (static check; no C compiler for the run)"

work=$(mktempdir)
cat >"$work/t.c" <<'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirror handle_status()'s grow branch: size the buffer for the flags string,
 * then strcpy the string (which writes strlen()+1 bytes, including the NUL). */
static int grow_and_copy(const char *flagstart)
{
	/* A canary byte right after the flags buffer, so a one-past write shows.
	 * The buffer is sized the fixed way: strlen()+1. */
	size_t n = strlen(flagstart);
	struct { char *buf; } holder;
	char *slab = malloc(n + 1 + 1);   /* +1 fixed sizing, +1 canary */
	if (!slab) return 2;
	slab[n + 1] = '#';                /* canary just past the fixed allocation */
	holder.buf = slab;

	strcpy(holder.buf, flagstart);    /* writes n+1 bytes */

	if (strcmp(holder.buf, flagstart) != 0) { fprintf(stderr, "flags corrupted\n"); free(slab); return 1; }
	if (holder.buf[n] != '\0')        { fprintf(stderr, "flags not terminated\n"); free(slab); return 1; }
	if (slab[n + 1] != '#')           { fprintf(stderr, "canary clobbered -- strcpy overran the fixed sizing\n"); free(slab); return 1; }
	free(slab);

	/* Characterise the bug without running it: strcpy writes n+1 bytes, so a
	 * buffer sized to strlen() alone (n) is short by exactly one. */
	if (!((n + 1) > n)) { fprintf(stderr, "sizing arithmetic wrong\n"); return 1; }
	return 0;
}

int main(void)
{
	/* A short flags string, then a longer one for the same host.test: the
	 * longer one is what drives handle_status() into the grow branch. */
	if (grow_and_copy("dg") != 0) return 1;
	if (grow_and_copy("dghij:kl:mn=1") != 0) return 1;
	return 0;
}
CEOF

"$CC" -std=c99 -Wall -Wextra -Werror -o "$work/t" "$work/t.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "status-flags grow probe did not compile"; }
"$work/t" || fail "status-flags grow probe failed: the grown buffer does not hold the flags plus its NUL"

pass "xymond.c handle_status() sizes the grown testflags buffer for the flags string and its NUL"
