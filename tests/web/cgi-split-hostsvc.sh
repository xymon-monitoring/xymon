#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/cgi-split-hostsvc.sh
#
# Compiles and runs cgi-split-hostsvc-harness.c against the real lib/cgi.c
# (via libxymon.a). Pins the confinement contract of cgi_pathcomponent()
# and cgi_split_hostsvc() -- history.cgi HISTFILE, reportlog.cgi HOSTSVC:
# traversal-shaped values are refused whole (#147 review: "realhost.cpu/.."
# must be refused, not served as "realhost.cpu"), while the legacy
# tolerances (trailing-junk truncation, charset-free names, per-parameter
# comma decoding) keep working.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"

work=$(mktempdir)

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a

"$CC" -I"$ROOT/include" -o "$work/harness" \
	"$(dirname "$0")/cgi-split-hostsvc-harness.c" "$ROOT/lib/libxymon.a" \
	2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" >"$work/run.log" 2>&1 \
	|| { cat "$work/run.log" >&2; fail "cgi_split_hostsvc() parse/reject behavior is broken"; }

echo "OK $(basename "$0")"
