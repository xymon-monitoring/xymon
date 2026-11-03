#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/updmanver-every-page.sh
#
# A release stamps "Version X.Y.Z: date" into every manual page source
# through build/updmanver, which walks a fixed list of directories. A page in
# a directory missing from that list kept the previous release's version
# while its HTML copy, which build/makehtml.sh reads from its own list, said
# the new one: lib/xymontools.1 stayed at 4.3.30.
#
# Checked on a copy of every manual page source in the tree -- found by its
# .TH first line, so a page added in a new directory fails here too: after
# updmanver, each one carries the version and the SOURCE_DATE_EPOCH date.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

work=$(mktempdir)
pages=""
for f in $(cd "$ROOT" && find . -name '*.[1-8]' -type f ! -path './tests/*' | sed 's|^\./||' | sort); do
	first=
	IFS= read -r first <"$ROOT/$f" || true
	case $first in '.TH '*) ;; *) continue ;; esac
	mkdir -p "$work/$(dirname "$f")"
	cp "$ROOT/$f" "$work/$f"
	pages="$pages $f"
done
[ -n "$pages" ] || fail "no manual page source found under $ROOT"

out=$(cd "$work" && SOURCE_DATE_EPOCH=0 "$ROOT/build/updmanver" 9.9.9 2>&1) \
	|| fail "updmanver failed: $out"

for f in $pages; do
	first=
	IFS= read -r first <"$work/$f" || true
	case $first in
		*'"Version 9.9.9:  1 Jan 1970" "Xymon"') ;;
		*) fail "$f was not stamped by updmanver: $first" ;;
	esac
done

pass "updmanver stamps the version into every manual page source ($(echo $pages | wc -w | tr -d ' ') pages)"
