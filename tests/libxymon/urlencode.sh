#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/urlencode.sh
#
# urlencode() writes every byte but a letter or a digit as "%" and two hex
# digits. It formatted the byte with "%0x" from a plain char: where char is
# signed (x86), a byte above 0x7f such as the 0xe9 of UTF-8 "é" came out as
# "%ffffffe9", cut to "%ff"; a byte below 0x10 such as a tab came out as "%9",
# and the NUL after it was stepped over, so the string ended there.
#
# Checked: "a b", a tab, "tést" in UTF-8, and a mix, each against its exact
# encoding, and that lib/cgi.c's urlunescape() decodes each back to the input.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a libxymon.a

cat >"$work/harness.c" <<'HARNESS'
#include <stdio.h>
#include <string.h>
#include "libxymon.h"

static int check(const char *in, const char *want)
{
	char buf[200], *got, *back;

	snprintf(buf, sizeof(buf), "%s", in);
	got = urlencode(buf);
	if (strcmp(got, want) != 0) { printf("urlencode(\"%s\") = \"%s\", want \"%s\"\n", in, got, want); return 1; }
	snprintf(buf, sizeof(buf), "%s", got);
	back = urlunescape(buf);
	if (strcmp(back, in) != 0) { printf("urlunescape(\"%s\") = \"%s\", want \"%s\"\n", want, back, in); return 1; }
	return 0;
}

int main(void)
{
	int fails = 0;

	fails += check("a b", "a%20b");
	fails += check("a\tb", "a%09b");
	fails += check("t\xc3\xa9st", "t%c3%a9st");
	fails += check("^x\\.y$\x01\xff", "%5ex%5c%2ey%24%01%ff");
	return fails;
}
HARNESS
pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || pcre_libs=$(xymon_makevar "$ROOT" PCRELIBS)
# shellcheck disable=SC2046,SC2086  # the flag lists are meant to split
"$CC" $(xymon_cflags "$ROOT") -o "$work/harness" "$work/harness.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"
out=$("$work/harness" 2>&1) || fail "$out"

pass "urlencode() writes each byte as two hex digits, above 0x7f and below 0x10 too, and urlunescape() reads it back"
