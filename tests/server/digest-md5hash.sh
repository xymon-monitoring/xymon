#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/digest-md5hash.sh
#
# Guard for the md5hash() buffer-size fix in lib/digest.c
# (xymon-monitoring/xymon#8).
#
# The hex-formatting loop computed snprintf's size argument as
#   sizeof(md_string) - (md_string - p)
# but `p` advances past `md_string`, so `md_string - p` is negative and the
# size argument (a size_t) wrapped to a value LARGER than the buffer --
# defeating snprintf's bounds protection. The fix flips it to `(p - md_string)`,
# the true bytes already written, so the remaining size is correct.
#
# md5hash() itself pulls in libxymon + the myMD5 backend, so we don't link it;
# instead we (1) bind to the real file -- the fixed operand order must be there
# and the buggy one gone -- and (2) compile+run a faithful copy of the loop:
# it must fill exactly 32 hex chars without touching a trailing canary, and the
# buggy size expression must be shown to exceed the buffer. Skips if the file is
# absent; the compile+run step skips if no C compiler.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/lib/digest.c"
CC="${CC:-cc}"

[ -f "$SRC" ] || skip "lib/digest.c absent"
src=$(cat "$SRC")

# (1) bind to the real code: fixed operand order present, buggy one gone.
assert_contains "sizeof(md_string) - (p - md_string)" "$src" \
	"digest.c md5hash() lost the corrected snprintf size operand order (#8)"
assert_not_contains "sizeof(md_string) - (md_string - p)" "$src" \
	"digest.c md5hash() regressed to the negative-offset size argument (#8)"

# (2) behavioural demo of the property, if we can compile.
if ! command -v "$CC" >/dev/null 2>&1; then
	exit 0
fi

WORK=$(mktempdir)
cat >"$WORK/t.c" <<'EOF'
#include <stdio.h>
#include <string.h>
int main(void)
{
	unsigned char md_value[16];
	struct { char md_string[2*16+1]; char canary; } s;
	int i; char *p;
	s.canary = '#';
	for (i = 0; i < 16; i++) md_value[i] = (unsigned char)((i << 4) | i);

	/* the real (fixed) loop from md5hash() */
	for (i = 0, p = s.md_string; (i < (int)sizeof(md_value)); i++)
		p += snprintf(p, (sizeof(s.md_string) - (p - s.md_string)), "%02x", md_value[i]);
	*p = '\0';

	if (strlen(s.md_string) != 32) { fprintf(stderr, "len=%zu\n", strlen(s.md_string)); return 1; }
	if (s.canary != '#')          { fprintf(stderr, "canary clobbered\n");             return 1; }

	/* prove the buggy operand order yields an out-of-bounds size argument */
	{
		char *q = s.md_string + 10;            /* mid-loop */
		size_t fixed = sizeof(s.md_string) - (q - s.md_string);
		size_t buggy = sizeof(s.md_string) - (s.md_string - q);
		if (!(fixed <= sizeof(s.md_string) && buggy > sizeof(s.md_string))) {
			fprintf(stderr, "fixed=%zu buggy=%zu\n", fixed, buggy);
			return 1;
		}
	}
	return 0;
}
EOF

if ! "$CC" -std=c99 -Wall -Wextra -Werror -o "$WORK/t" "$WORK/t.c" 2>"$WORK/err"; then
	fail "md5hash format probe did not compile:
$(cat "$WORK/err")"
fi
"$WORK/t" || fail "md5hash format probe failed: the loop overran or the size property broke (#8)"

exit 0
