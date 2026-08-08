#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/netrc-password.sh
#
# Regression guard for the off-by-one in load_netrc() that dropped the last
# character of every .netrc password (commit b4c4775dc).
#
# load_netrc() packs "login:password" into item->auth. It allocated the right
# size -- malloc(login_len + 1), where login_len = strlen(login) + strlen(pw)
# + 1 for the ':' -- but passed login_len (one byte short) as snprintf()'s
# size, so snprintf's own NUL landed on the password's last character and
# silently truncated it. The fix passes login_len + 1.
#
# load_netrc() is static and pulls the whole URL machinery in, so -- like
# tests/server/digest-md5hash.sh -- this (1) binds to the real source: the
# corrected size argument must be present and the short one gone, and (2)
# compiles a faithful copy of the pack step and shows the fixed size yields the
# whole password while the buggy size truncates it. Skips if the file or a C
# compiler is absent.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/lib/url.c"
CC=${CC:-cc}

[ -f "$SRC" ] || skip "lib/url.c absent"
src=$(cat "$SRC")

# (1) Bind to the real code: fixed size argument present, buggy one gone. The
# fixed line is `snprintf(item->auth, login_len + 1, ...)`; the bug was
# `snprintf(item->auth, login_len, ...)`. The " + 1," / "," suffixes keep the
# two apart so the fixed form cannot satisfy the buggy pattern.
assert_contains "snprintf(item->auth, login_len + 1," "$src" \
	"url.c load_netrc() lost the corrected snprintf size (login_len + 1) (#226 / b4c4775dc)"
assert_not_contains "snprintf(item->auth, login_len," "$src" \
	"url.c load_netrc() regressed to the one-short snprintf size (login_len) (#226 / b4c4775dc)"

# (2) Behavioural demo of the property, if we can compile.
command -v "$CC" >/dev/null 2>&1 \
	|| pass "url.c keeps the #226 fix (static check; no C compiler for the run)"

work=$(mktempdir)
cat >"$work/t.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A faithful copy of the load_netrc() pack step for one entry. */
static char *pack(const char *login, const char *password, int use_fix)
{
	unsigned int login_len = strlen(login) + strlen(password) + 1;  /* +1 for ':' */
	char *auth = (char *)malloc(login_len + 1);
	snprintf(auth, use_fix ? (login_len + 1) : login_len, "%s:%s", login, password);
	return auth;
}

int main(void)
{
	const char *login = "user";
	const char *password = "s3cr3t";        /* last char 't' is what got dropped */
	char want[64];
	char *fixed, *buggy;

	snprintf(want, sizeof(want), "%s:%s", login, password);

	fixed = pack(login, password, 1);
	buggy = pack(login, password, 0);

	/* Fixed: the whole "login:password" survives, last char included. */
	if (strcmp(fixed, want) != 0) {
		fprintf(stderr, "fixed size mangled the pair: got '%s', want '%s'\n", fixed, want);
		return 1;
	}
	/* Buggy: exactly the off-by-one symptom -- last char of the password lost. */
	if (strcmp(buggy, want) == 0) {
		fprintf(stderr, "buggy size did not truncate -- the demo no longer models #226\n");
		return 1;
	}
	if (strlen(buggy) != strlen(want) - 1) {
		fprintf(stderr, "buggy size truncated by %zu chars, expected 1\n", strlen(want) - strlen(buggy));
		return 1;
	}
	return 0;
}
EOF

"$CC" -std=c99 -Wall -Wextra -Werror -o "$work/t" "$work/t.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "netrc pack probe did not compile"; }
"$work/t" || fail "netrc pack probe failed: the size-argument property broke (#226)"

pass "load_netrc() packs the full login:password; the one-short size truncates it (#226)"
