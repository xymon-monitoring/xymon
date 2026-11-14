#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/cgi-pageregex.sh
#
# Compiles and runs cgi-pageregex-harness.c against the real lib/cgi.c and
# lib/matching.c (via libxymon.a). cgi_pageregex() is the "page=" filter the
# CGIs build from the "pagepath" cookie (#797): the page the viewer came from
# and its sub-pages, and no other -- "ops.eu" also matched "opsXeu", and
# acknowledge.cgi's "ops$|^ops/.+" also matched "devops" -- and it refuses a
# value that would split the query.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"

work=$(mktempdir)

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a

pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")

# shellcheck disable=SC2086
"$CC" $harness_cflags -o "$work/harness" \
	"$(dirname "$0")/cgi-pageregex-harness.c" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" >"$work/run.log" 2>&1 \
	|| { cat "$work/run.log" >&2; fail "cgi_pageregex() does not make a page path a literal pattern"; }

pass "cgi_pageregex(): a page path becomes a pattern for that page and its sub-pages only, and a space or control character is refused"
