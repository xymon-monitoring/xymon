#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-cgi-link-failure.sh
#
# web/Makefile's install-cgi hard-links cgiwrap to every CGI name. make only
# sees the status of a loop's last iteration, so a link that failed earlier
# left the package short of a CGI while the build reported success -- and the
# missing name 404s on a live server, with nothing in the build log.
#
# Observed on a Homebrew macOS keg: history.sh and eventlog.sh, the first two
# names in CGISCRIPTS, were absent while the install reported success.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/web/Makefile" ] || skip "web/Makefile absent"
require_gnu_make

# "-o install-bin": install-cgi depends on install-bin, which chowns and copies
# the built CGIs. This test is about install-cgi's own recipe and supplies its
# own cgiwrap, so the prerequisite is declared up to date rather than run - it
# would otherwise need a built tree and a real XYMONUSER.
run_install_cgi() {  # run_install_cgi <workdir> -- prints nothing, returns make's status
	local w=$1
	"$XYMON_MAKE" -C "$ROOT/web" -o install-bin install-cgi \
		INSTALLROOT= INSTALLBINDIR="$w/bin" \
		CGIDIR="$w/cgi" SECURECGIDIR="$w/seccgi" >"$w/make.log" 2>&1
}

# ---- a link that cannot be made must stop the install ----------------------
# The first CGI name is obstructed by an unwritable directory, so its ln fails
# while every later one succeeds -- the shape that used to pass unnoticed.
work=$(mktempdir)
mkdir -p "$work/bin" "$work/cgi" "$work/seccgi"
: >"$work/bin/cgiwrap"
first=$(sed -n 's/^CGISCRIPTS *= *//p' "$ROOT/web/Makefile" | awk '{print $1}')
[ -n "$first" ] || fail "could not read the first CGISCRIPTS entry from web/Makefile"
mkdir "$work/cgi/$first"
chmod 500 "$work/cgi/$first"

rc=0; run_install_cgi "$work" || rc=$?
chmod 700 "$work/cgi/$first"
[ "$rc" -ne 0 ] \
	|| fail "install-cgi reported success although the link for '$first' could not be made: $(cat "$work/make.log")"

# ---- and an unobstructed run still links every name ------------------------
# Without this the check above could be satisfied by a rule that always fails.
work2=$(mktempdir)
mkdir -p "$work2/bin" "$work2/cgi" "$work2/seccgi"
: >"$work2/bin/cgiwrap"

run_install_cgi "$work2" \
	|| fail "install-cgi failed on a clean destination: $(cat "$work2/make.log")"

for v in CGISCRIPTS SECCGISCRIPTS; do
	case $v in CGISCRIPTS) d=$work2/cgi ;; *) d=$work2/seccgi ;; esac
	for n in $(sed -n "s/^$v *= *//p" "$ROOT/web/Makefile"); do
		[ -f "$d/$n" ] || fail "install-cgi did not link $n into $(basename "$d")"
	done
done

pass "install-cgi stops when a CGI link cannot be made, and links every name otherwise"
