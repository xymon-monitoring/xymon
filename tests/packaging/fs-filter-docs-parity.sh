#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/packaging/fs-filter-docs-parity.sh
#
# Shipped-file invariant: every client OS script that consumes the
# XYMONCLIENT_FS_* filesystem-filter variables must be documented, per OS, in
# BOTH halves of the config/manual pair - a "<OS> (xymonclient-<os>.sh)"
# defaults stub in client/xymonclient.cfg.DIST, and an OS paragraph in
# common/xymonclient.cfg.5, which is the canonical full contract. The two
# files ship identically to every OS, so a per-OS port that lands without
# its doc sections leaves admins reading another OS's defaults (this
# happened: FreeBSD shipped with neither, macOS with only the cfg.DIST
# half).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
DIST="$ROOT/client/xymonclient.cfg.DIST"
MAN="$ROOT/common/xymonclient.cfg.5"

assert_file_exists "$DIST"
assert_file_exists "$MAN"
dist=$(cat "$DIST")
man=$(cat "$MAN")

checked=0
for script in "$ROOT"/client/xymonclient-*.sh; do
	grep -q 'XYMONCLIENT_FS_' "$script" || continue
	os=$(basename "$script" .sh); os=${os#xymonclient-}
	case "$os" in
		linux)   label="Linux" ;;
		freebsd) label="FreeBSD" ;;
		netbsd)  label="NetBSD" ;;
		openbsd) label="OpenBSD" ;;
		darwin)  label="macOS" ;;
		sunos)   label="Solaris" ;;
		aix)     label="AIX" ;;
		hp-ux)   label="HP-UX" ;;
		*) fail "no OS label mapping for client script '$script' - extend this test" ;;
	esac

	assert_contains "$label (xymonclient-$os.sh)" "$dist" \
		"xymonclient.cfg.DIST lacks the \"$label (xymonclient-$os.sh)\" defaults stub for $script"
	assert_contains "$label" "$man" \
		"xymonclient.cfg.5 does not mention $label although $script uses the XYMONCLIENT_FS_ variables"
	checked=$((checked + 1))
done

[ "$checked" -gt 0 ] || fail "no client scripts using XYMONCLIENT_FS_ found - test is miswired"

pass "FS-filter docs cover all $checked participating client OSes in cfg.DIST and cfg.5"
