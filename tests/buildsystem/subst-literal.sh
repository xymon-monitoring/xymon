#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/subst-literal.sh
#
# build/subst.sh replaces each @NAME@ in a template with the value of the
# environment variable NAME, literally, for the config files and scripts the
# Makefiles generate from their .DIST templates. It replaces
# "sed -e 's!@X@!$(X)!g'", which silently mangled a value carrying a sed
# metacharacter: "&" expands to the matched text, "!" ends the replacement, a
# backslash is interpreted; the unquoted recipe broke on a space too. These are
# rare in a Unix path or user but normal elsewhere.
#
# Pins: the literal substitution (the "&" case sed gets wrong), a value with a
# backslash and a space, an empty value, and an unknown @NAME@ and a stray "@"
# left as they are. The values reach the helper through the environment, so no
# sed, shell word split or awk escaping touches them.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
subst="$ROOT/build/subst.sh"
[ -x "$subst" ] || fail "build/subst.sh is missing or not executable"

tmpl=$(mktempdir)/tmpl
printf 'User=@XYMONUSER@\nPath=@XYMONHOME@/bin\nFlags=[@CLIENTFLAGS@]\nKept=@UNKNOWN@ @ @@ end\n' > "$tmpl"

out=$(XYMONUSER='a&b!c' XYMONHOME='C:\Prog z' CLIENTFLAGS='' \
	"$subst" XYMONUSER XYMONHOME CLIENTFLAGS < "$tmpl")

grep -qxF 'User=a&b!c' <<<"$out" \
	|| fail "a value with & and ! was not substituted literally:
$out"
grep -qxF 'Path=C:\Prog z/bin' <<<"$out" \
	|| fail "a value with a backslash and a space was not substituted:
$out"
grep -qxF 'Flags=[]' <<<"$out" \
	|| fail "an empty value was not substituted to nothing:
$out"
grep -qxF 'Kept=@UNKNOWN@ @ @@ end' <<<"$out" \
	|| fail "an unknown placeholder, a stray @ and @@ were not left alone:
$out"

# No Makefile should have gone back to sed on an @VAR@: that is the mangling
# this replaces.
hits=$(grep -rlE "sed -e 's.@[A-Z_]+@" "$ROOT"/*/Makefile "$ROOT"/build/Makefile.rules 2>/dev/null || true)
[ -z "$hits" ] \
	|| fail "a Makefile still substitutes an @VAR@ with sed instead of build/subst.sh:
$hits"

pass "build/subst.sh substitutes @NAME@ from the environment literally where sed mangled &, !, backslash and spaces, leaves an unknown placeholder alone, and no Makefile uses sed for @VAR@"
