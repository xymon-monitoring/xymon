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
# + 1 for the ':' -- but passed login_len (one byte short) to snprintf(), so
# snprintf's NUL landed on the password's last character and truncated it. The
# fix passes login_len + 1.
#
# Two layers, so coverage does not depend on the tree being built:
#   (1) A build-independent source check that the snprintf() size is login_len
#       + 1 and not the one-short login_len. Runs with no compiler.
#   (2) A behavioural check, when a compiler is available: parse_url() runs the
#       real (static) load_netrc() and hands back the packed credential in
#       url.auth, so any regression that SHORTENS it -- the length calc, the
#       snprintf size, or a format change -- fails here, not just the snprintf
#       argument that (1) pins. (An undersized malloc overflows rather than
#       truncates, so it is out of scope for this content check.)
#
# Skips only when lib/url.c itself is absent.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
CC=${CC:-cc}

SRC="$ROOT/lib/url.c"
[ -f "$SRC" ] || skip "lib/url.c absent"
src=$(cat "$SRC")

# (1) Build-independent source guard: the snprintf() size must be login_len + 1.
# The " + 1," / "," suffixes keep the fixed and buggy forms apart so the fixed
# line cannot satisfy the buggy pattern.
assert_contains "snprintf(item->auth, login_len + 1," "$src" \
	"url.c load_netrc() lost the corrected snprintf size (login_len + 1) (#226 / b4c4775dc)"
assert_not_contains "snprintf(item->auth, login_len," "$src" \
	"url.c load_netrc() regressed to the one-short snprintf size (#226 / b4c4775dc)"

# (2) Behavioural check, when a compiler and the prebuilt library are present.
# It compiles the CURRENT lib/url.c fresh into the harness (so the result
# reflects the source, not a possibly-stale url.o inside the archive) and links
# the rest from the archive -- no make, no writable tree. If either is missing,
# the source guard above already stands, so pass with a note.
command -v "$CC" >/dev/null 2>&1 \
	|| pass "url.c keeps the #226 snprintf size (source check; no C compiler for the behavioural run)"
[ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| pass "url.c keeps the #226 snprintf size (source check; library not built for the behavioural run)"

work=$(mktempdir)

# Link flags for the prebuilt library. Tolerate an absent top-level Makefile
# (e.g. only lib/ configured): fall back to no extra SSL flags rather than
# letting sed abort the script under 'set -e'.
ssllibs=""
[ -f "$ROOT/Makefile" ] && ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")
pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "libxymon.h"

/* Drive the real parse_url(), which loads the .netrc via load_netrc() and
 * hands back the packed "login:password" in url.auth. */
int main(int argc, char *argv[])
{
	urlelem_t url;

	memset(&url, 0, sizeof(url));
	parse_url(argv[1], &url);
	printf("%s\n", url.auth ? url.auth : "");
	return 0;
}
EOF

# lib/url.c goes before the archive so its fresh objects satisfy the netrc
# symbols and the archive's (possibly stale) url.o is never pulled in.
harness_cflags=$(xymon_cflags "$ROOT")
"$CC" $harness_cflags -o "$work/harness" \
	"$work/harness.c" "$SRC" "$ROOT/lib/libxymoncomm.a" $ssllibs $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "netrc harness does not compile"; }

# A password whose last byte matters: the off-by-one drops the trailing 't'.
login="user"
password="s3cr3t"
printf 'machine testhost login %s password %s\n' "$login" "$password" >"$work/.netrc"
chmod 600 "$work/.netrc"

# Point load_netrc() at our fixture: it reads $XYMONHOME/etc/netrc first, then
# $HOME/.netrc -- give XYMONHOME a dir with no etc/netrc so it falls through.
got=$(HOME="$work" XYMONHOME="$work/empty" "$work/harness" "http://testhost/" 2>"$work/run.log") \
	|| { cat "$work/run.log" >&2; fail "harness run failed"; }

want="$login:$password"
assert_equal "$want" "$got" \
	"load_netrc() packed the wrong credential -- the .netrc password off-by-one regressed (#226 / b4c4775dc)"

pass "load_netrc() packs the full login:password end to end; the last character survives (#226)"
