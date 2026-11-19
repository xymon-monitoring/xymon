#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/subst.sh
#
# build/subst replaces @NAME@ placeholders in a template with a value literally,
# for the config files and scripts the Makefiles generate from their .DIST
# templates. It replaces "sed -e 's!@X@!$(X)!g'", which silently mangled a value
# carrying a sed metacharacter: "&" expands to the matched text, "!" ends the
# replacement, a backslash is interpreted. A path with a space -- normal off
# Unix -- broke the surrounding unquoted recipe too.
#
# Pins: the literal substitution (the "&" case sed gets wrong), a value with a
# space, an unknown @NAME@ and a stray "@" left as they are, and that no Makefile
# has gone back to sed for an @VAR@ (which would reintroduce the mangling).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
"$CC" -o "$work/subst" "$ROOT/build/subst.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "build/subst.c does not compile"; }

printf 'User=@XYMONUSER@\nPath=@XYMONHOME@/bin\nKept=@UNKNOWN@ @\n' > "$work/tmpl"
out=$("$work/subst" 'XYMONUSER=a&b!c' 'XYMONHOME=C:\Prog z' < "$work/tmpl")

grep -qxF 'User=a&b!c' <<<"$out" \
	|| fail "a value with & and ! was not substituted literally:
$out"
grep -qxF 'Path=C:\Prog z/bin' <<<"$out" \
	|| fail "a path with a backslash and a space was not substituted:
$out"
grep -qxF 'Kept=@UNKNOWN@ @' <<<"$out" \
	|| fail "an unknown placeholder and a stray @ were not left alone:
$out"

# The point of the helper is that nothing generates config with sed on @VAR@
# any more; a Makefile that does would silently mangle those values again.
hits=$(grep -rlE "sed -e 's.@[A-Z_]+@" "$ROOT"/*/Makefile "$ROOT"/build/Makefile.rules 2>/dev/null || true)
[ -z "$hits" ] \
	|| fail "a Makefile still substitutes an @VAR@ with sed instead of build/subst:
$hits"

pass "build/subst substitutes @NAME@ literally where sed mangled &, !, backslash and spaces, leaves an unknown placeholder alone, and no Makefile uses sed for @VAR@"
