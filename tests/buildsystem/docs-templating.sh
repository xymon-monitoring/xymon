#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/docs-templating.sh
#
# Behavioural guard for the doc templating added by
# xymon-monitoring/xymon#90, as it stands now:
#
#   - xymon-apacheconf.txt is generated from its .DIST template by
#     docs/Makefile, with the install paths and the three URLs configure asks
#     for (web pages, CGI, administration CGI) substituted, and no
#     @PLACEHOLDER@ left over. The URLs used to be written out as /xymon/,
#     /xymon-cgi/ and /xymon-seccgi/, so the example did not match an
#     installation with other answers.
#   - install.html is a plain file, not a template: it is read before
#     configure runs, on GitHub or in the tarball, where a placeholder would
#     show as @XYMONUSER@. So it carries no placeholder, and "make clean" must
#     not delete it.
#
# We copy the Makefile, the template and the guide into a temp dir and run the
# real recipes with known values. Working in a copy keeps the repo tree clean.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
DOCS="$ROOT/docs"

require_gnu_make
# These are the repository's own files: one missing is a failure, not a
# reason to skip.
for f in Makefile install.html xymon-apacheconf.txt.DIST; do
	[ -f "$DOCS/$f" ] || fail "docs/$f is missing"
done

work=$(mktempdir)
cp "$DOCS/Makefile" "$DOCS/install.html" "$DOCS/xymon-apacheconf.txt.DIST" "$work/"
conf="$work/xymon-apacheconf.txt"

# generate URL -- run the real recipe, the web pages at URL
generate() {
	rm -f "$conf"
	"$XYMON_MAKE" -C "$work" xymon-apacheconf.txt \
		INSTALLETCDIR=/etc/xymon INSTALLWWWDIR=/var/www/xymon \
		CGIDIR=/cgi SECURECGIDIR=/secure-cgi \
		XYMONHOSTURL="$1" XYMONCGIURL=/mon-cgi SECUREXYMONCGIURL=/mon-seccgi \
		>"$work/make.log" 2>&1 \
		|| fail "docs templating recipe failed for web URL '${1:-/}':
$(cat "$work/make.log")"
	assert_file_exists "$conf" "docs templating did not generate xymon-apacheconf.txt (#90)"
}

generate /mon
leftover=$(grep -oE '@[A-Z_]+@' "$conf" || true)
[ -z "$leftover" ] || fail "xymon-apacheconf.txt still has unsubstituted placeholders (#90): $leftover"
assert_contains 'ScriptAlias /mon-cgi/ "/cgi/"' "$(cat "$conf")" \
	"xymon-apacheconf.txt does not serve the CGIs at the configured URL"
assert_contains 'ScriptAlias /mon-seccgi/ "/secure-cgi/"' "$(cat "$conf")" \
	"xymon-apacheconf.txt does not serve the administration CGIs at the configured URL"

# The web pages: a literal Alias for the configured URL -- "/mon+" is a URL,
# not a regular expression -- and at the root (configure turns the answer
# "/" into an empty XYMONHOSTURL) 'Alias / "<www dir>/"'. At the root it
# matches every URL, and Apache takes the first alias that matches, so it
# must come after both ScriptAlias lines or it swallows the CGI URLs.
for url in /mon /mon+ ''; do
	generate "$url"
	if [ -n "$url" ]; then
		wurl=$url wdir='"/var/www/xymon"'
	else
		wurl=/ wdir='"/var/www/xymon/"'
	fi
	web=$(awk -v u="$wurl" -v d="$wdir" '$1 == "Alias" && $2 == u && $3 == d && NF == 3 { print NR; exit }' "$conf")
	[ -n "$web" ] || fail "web URL '${url:-/}': xymon-apacheconf.txt does not map the web pages by: Alias $wurl $wdir"
	grep -q '^AliasMatch' "$conf" \
		&& fail "web URL '${url:-/}': an AliasMatch reads the URL as a regular expression"
	for cgi in $(grep -n '^ScriptAlias ' "$conf" | cut -d: -f1); do
		[ "$cgi" -lt "$web" ] || fail "web URL '${url:-/}': xymon-apacheconf.txt puts the web pages' Alias (line $web) before a ScriptAlias (line $cgi), which at the root it would swallow"
	done
done

# The top-level build runs these recipes through build/Makefile.rules'
# docs-build, which passes the configured values on one by one. A value the
# recipes use but docs-build does not pass is substituted as empty in a real
# build -- the copy above, which sets every value itself, would not notice.
rule=$(grep -A1 '^docs-build:' "$ROOT/build/Makefile.rules" | tail -n 1)
for var in $(grep -oE '!\$\([A-Z_]+\)!' "$DOCS/Makefile" | tr -d '!$()' | sort -u); do
	# A value docs/Makefile computes itself (XYMONWEBALIAS) is not passed in.
	grep -q "^$var =" "$DOCS/Makefile" && continue
	case "$rule" in
		*" $var=\"\$($var)\""*|*"	$var=\"\$($var)\""*) ;;
		*) fail "docs/Makefile substitutes \$($var), but build/Makefile.rules' docs-build does not pass it" ;;
	esac
done

leftover=$(grep -oE '@[A-Z_]+@' "$DOCS/install.html" || true)
[ -z "$leftover" ] || fail "install.html is read before configure runs, but carries placeholders: $leftover"

"$XYMON_MAKE" -C "$work" clean >"$work/clean.log" 2>&1 \
	|| fail "make clean failed: $(cat "$work/clean.log")"
assert_file_exists "$work/install.html" "make clean deleted install.html, which is not generated"

pass "xymon-apacheconf.txt carries the configured paths and URLs, and install.html is a plain file make clean keeps"
