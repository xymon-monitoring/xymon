#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/cgioptions-datepage-url.sh
#
# datepage.cgi sends the browser to the periodic report it picked, at the
# URL its --url option names plus /daily/..., /weekly/... or /monthly/...
# (web/datepage.c). cgioptions.cfg gave it --url=/xymon/periodic written
# out, so on an installation whose web pages are not at /xymon -- configure
# asks -- every periodic report link led nowhere. The reports themselves are
# published under $XYMONWEB/periodic (xymond/xymonreports.sh.DIST), which
# follows the configured URL.
#
# The real cfgfiles recipe of xymond/Makefile is run on a copy, with the web
# pages at /mon, and the generated cgioptions.cfg must give datepage.cgi
# --url=/mon/periodic, with no placeholder left.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_gnu_make

work=$(mktempdir)
mkdir -p "$work/xymond" "$work/build"
cp "$ROOT/xymond/Makefile" "$work/xymond/"
cp -R "$ROOT/xymond/etcfiles" "$work/xymond/"
cp "$ROOT/build/bb-commands.sh" "$work/build/"

"$XYMON_MAKE" -C "$work/xymond" cfgfiles \
	XYMONHOME=/opt/xymon/server XYMONTOPDIR=/opt/xymon INSTALLETCDIR=/opt/xymon/server/etc \
	INSTALLWWWDIR=/opt/xymon/server/www CGIDIR=/cgi SECURECGIDIR=/secure-cgi \
	XYMONHOSTURL=/mon XYMONCGIURL=/mon-cgi SECUREXYMONCGIURL=/mon-seccgi \
	>"$work/make.log" 2>&1 \
	|| fail "xymond cfgfiles recipe failed:
$(cat "$work/make.log")"

cfg="$work/xymond/etcfiles/cgioptions.cfg"
assert_file_exists "$cfg" "the cfgfiles recipe did not generate cgioptions.cfg"
leftover=$(grep -oE '@[A-Z_]+@' "$cfg" || true)
[ -z "$leftover" ] || fail "cgioptions.cfg still has unsubstituted placeholders: $leftover"
assert_contains 'CGI_DATEPAGE_OPTS="--env=$XYMONENV --url=/mon/periodic"' "$(cat "$cfg")" \
	"datepage.cgi is not sent to the periodic reports at the configured web URL"

pass "cgioptions.cfg gives datepage.cgi the periodic reports at the configured web URL"
