#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/docs-templating.sh
#
# Behavioural guard for the doc templating added by
# xymon-monitoring/xymon#90: install.html and xymon-apacheconf.txt are now
# generated from *.DIST templates by docs/Makefile, substituting @VAR@
# placeholders (install dirs, CGI dirs, ...) via sed.
#
# We copy the Makefile and the two .DIST templates into a temp dir and run the
# real recipes with known values, then assert the outputs carry the substituted
# values and -- the point of the fix -- carry no leftover @PLACEHOLDER@. Working
# in a copy keeps the repo tree clean. Skips if make or the templates are absent.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
DOCS="$ROOT/docs"
MAKE="${MAKE:-make}"

command -v "$MAKE" >/dev/null 2>&1 || skip "no make"
for f in Makefile install.html.DIST xymon-apacheconf.txt.DIST; do
	[ -f "$DOCS/$f" ] || skip "docs/$f absent -- predates #90"
done

work=$(mktempdir)
cp "$DOCS/Makefile" "$DOCS/install.html.DIST" \
   "$DOCS/xymon-apacheconf.txt.DIST" "$work/"

"$MAKE" -C "$work" install.html xymon-apacheconf.txt \
	INSTALLETCDIR=/etc/xymon XYMONTOPDIR=/opt/xymon XYMONUSER=xymonuser \
	INSTALLWWWDIR=/var/www/xymon CGIDIR=/cgi SECURECGIDIR=/secure-cgi \
	>"$work/make.log" 2>&1 \
	|| fail "docs templating recipe failed:
$(cat "$work/make.log")"

for out in install.html xymon-apacheconf.txt; do
	assert_file_exists "$work/$out" "docs templating did not generate $out (#90)"
	# the substituted values landed
	# (no leftover @PLACEHOLDER@ tokens -- that was the symptom #90 fixed)
	leftover=$(grep -oE '@[A-Z_]+@' "$work/$out" || true)
	[ -z "$leftover" ] || fail "$out still has unsubstituted placeholders (#90): $leftover"
done

assert_contains "/opt/xymon" "$(cat "$work/install.html")" \
	"install.html did not substitute XYMONTOPDIR (#90)"
assert_contains "/var/www/xymon" "$(cat "$work/xymon-apacheconf.txt")" \
	"xymon-apacheconf.txt did not substitute INSTALLWWWDIR (#90)"

pass "docs templating substitutes @VARs@ with no leftover placeholders"
