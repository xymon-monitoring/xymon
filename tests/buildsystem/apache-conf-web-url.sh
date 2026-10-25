#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/apache-conf-web-url.sh
#
# configure asks for the web pages' URL, and "/" puts them at the root: it
# leaves XYMONHOSTURL empty. The generated xymon-apache.conf then read
# 'Alias  "<www dir>"', one argument, and Apache refused to start ("Alias
# must have two arguments"); its header told the administrator to replace
# the line with DocumentRoot by hand.
#
# Now the web pages are mapped by
#     AliasMatch "^<url>(/.*)?$" "<www dir>$1"
# which also takes the URL without its trailing slash, as the plain Alias
# did, and works with an empty URL. Apache uses the first alias that
# matches, and at the root this one matches every URL, so it must come after
# every ScriptAlias, or it takes the CGI URLs too.
#
# The real cfgfiles recipe of xymond/Makefile is run on a copy, for both
# templates (administration CGIs in their own URL or not) and both URLs, and
# the generated file is checked for those three things. Apache itself is not
# run here: it is rarely installed where the suite runs.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_gnu_make

work=$(mktempdir)

# generate URL SECURECGIURL -- the xymon-apache.conf the recipe writes
generate() {
	rm -rf "$work/tree"
	mkdir -p "$work/tree/xymond" "$work/tree/build"
	cp "$ROOT/xymond/Makefile" "$work/tree/xymond/"
	cp -R "$ROOT/xymond/etcfiles" "$work/tree/xymond/"
	cp "$ROOT/build/bb-commands.sh" "$work/tree/build/"
	"$XYMON_MAKE" -C "$work/tree/xymond" cfgfiles \
		XYMONHOME=/opt/xymon/server XYMONTOPDIR=/opt/xymon INSTALLETCDIR=/opt/xymon/server/etc \
		INSTALLWWWDIR=/opt/xymon/server/www CGIDIR=/cgi SECURECGIDIR=/secure-cgi \
		XYMONHOSTURL="$1" XYMONCGIURL=/xymon-cgi SECUREXYMONCGIURL="$2" \
		>"$work/make.log" 2>&1 \
		|| fail "xymond cfgfiles recipe failed:
$(cat "$work/make.log")"
	cat "$work/tree/xymond/etcfiles/xymon-apache.conf"
}

for secure in /xymon-seccgi /xymon-cgi; do
	for url in /xymon ''; do
		label="web URL '${url:-/}', administration CGIs at $secure"
		conf=$(generate "$url" "$secure")

		leftover=$(grep -oE '@[A-Z_]+@' <<<"$conf" || true)
		[ -z "$leftover" ] || fail "$label: unsubstituted placeholders: $leftover"

		web=$(grep -n "^AliasMatch \"^$url(/\\.\\*)?\\$\" \"/opt/xymon/server/www\\\$1\"\$" <<<"$conf" || true)
		web=${web%%:*}
		[ -n "$web" ] || fail "$label: the web pages are not mapped by AliasMatch \"^$url(/.*)?\$\""
		grep -qE '^[[:space:]]*Alias[[:space:]]' <<<"$conf" \
			&& fail "$label: a plain Alias is left, which an empty URL turns into a one-argument line Apache refuses"

		cgis=$(grep -n '^ScriptAlias ' <<<"$conf" || true)
		for cgi in $(cut -d: -f1 <<<"$cgis"); do
			[ "$cgi" -lt "$web" ] || fail "$label: the web pages' AliasMatch (line $web) comes before a ScriptAlias (line $cgi), which at the root it would take"
		done
	done
done

pass "xymon-apache.conf maps the web pages at the configured URL, the root included, after the CGI aliases"
