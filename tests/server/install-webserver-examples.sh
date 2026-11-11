#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/install-webserver-examples.sh
#
# A server install writes examples for lighttpd, nginx and caddy into
# examples/ in the server tree, next to bin/ and etc/'s xymon-apache.conf,
# with this build's paths filled in. Nothing reads them, so they stay out
# of etc/, where Xymon's own configuration is, and a reinstall replaces
# them: they are copied from, never edited in place.
#
# Staged under INSTALLROOT with PKGBUILD, as a package build installs, so
# nothing is chowned and nothing outside the stage is written.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_gnu_make
require_bin XYMOND xymond/xymond

work=$(mktempdir)
stage=$work/stage

install_into() {  # install_into LOG -- a staged install of the built tree
	"$XYMON_MAKE" -C "$ROOT" install PKGBUILD=1 INSTALLROOT="$stage" </dev/null >"$1" 2>&1 \
		|| fail "make install failed: $(tail -5 "$1")"
}

install_into "$work/install1.log"

apache=$(find "$stage" -name xymon-apache.conf -type f)
[ -n "$apache" ] || fail "the install wrote no xymon-apache.conf"
etc=$(dirname "$apache")
examples=$(find "$stage" -type d -path '*/server/examples')
[ -n "$examples" ] || fail "the install made no server/examples directory"

for s in lighttpd nginx caddy; do
	f=$examples/xymon-$s.conf
	[ -f "$f" ] || fail "no $s example in $examples"
	[ ! -e "$etc/xymon-$s.conf" ] || fail "the $s example was also installed into etc/ ($etc)"
	left=$(grep -o '@[A-Z][A-Z]*@' "$f" || true)
	[ -z "$left" ] || fail "the $s example kept placeholders: $left"
	grep -q '/cgi-bin' "$f" || fail "the $s example names no CGI directory"
done

echo 'edited' >"$examples/xymon-nginx.conf"
install_into "$work/install2.log"
grep -qx 'edited' "$examples/xymon-nginx.conf" \
	&& fail "a reinstall left an edited example in place instead of replacing it"

pass "the lighttpd, nginx and caddy examples go to server/examples with this build's paths, not to etc/, and a reinstall replaces them"
