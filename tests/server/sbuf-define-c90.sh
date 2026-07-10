#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/sbuf-define-c90.sh
#
# The SBUF_DEFINE/STATIC_SBUF_DEFINE macros must not end in a semicolon:
# every call site writes its own, and a doubled ';' is an empty STATEMENT,
# turning any following declaration into a C90 constraint violation. That
# stays invisible until something compiles with
# -Werror=declaration-after-statement - which newer net-snmp packages leak
# through `net-snmp-config --cflags`, breaking xymon-snmpcollect.c on
# Arch, Alpine 3.23, openSUSE Tumbleweed and FreeBSD 13.5-15.0.
#
# Guard: compile a snippet that uses the macro followed by a declaration,
# with that -Werror flag, against the real lib/strfunc.h.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktempdir)

cat >"$work/sbuf-c90.c" <<'EOF'
#include <stdlib.h>

/* strfunc.h only names strbuffer_t through pointers in prototypes */
typedef struct strbuffer_t strbuffer_t;
#include "strfunc.h"

int check(void);
int check(void)
{
	SBUF_DEFINE(buf);
	int used = 0;	/* must still be a legal declaration after SBUF_DEFINE(...); */

	SBUF_MALLOC(buf, 8);
	used = (buf != NULL);
	free(buf);
	return used + (int)buf_buflen;
}
EOF

"$CC" -fsyntax-only -Wall -Werror=declaration-after-statement \
	-I"$ROOT/lib" "$work/sbuf-c90.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; \
	     fail "SBUF_DEFINE followed by a declaration trips -Werror=declaration-after-statement (macro ends in ';'?)"; }

pass "SBUF_DEFINE call sites stay C90-clean under -Werror=declaration-after-statement"
