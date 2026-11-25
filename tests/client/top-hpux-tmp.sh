#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/top-hpux-tmp.sh
#
# HP-UX top cannot have its output redirected, so the HP-UX client has it
# write the [top] section to a file, prints the file and removes it. The file
# goes in XYMONTMP, the tmp directory xymonclient.cfg names, as every other
# temporary file of the client does. It went in XYMONHOME/tmp whatever the
# file said, so a client whose file names another tmp directory lost its
# [top] section without a link left at XYMONHOME/tmp.
#
# The block is cut out of the script and run with TOP pointing at a stub
# that writes the file named by -f, as the real one does, and XYMONHOME
# holding no tmp directory. Pinned here: the section is printed, from a file
# in XYMONTMP, and that file is removed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SCRIPT="$ROOT/client/xymonclient-hp-ux.sh"
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

work=$(mktempdir)
SNIPPET="$work/top-section.sh"
mkdir -p "$work/home" "$work/tmp"

# From the block's comment to the line before the vmstat one.
sed -n '/^# \$TOP must be set/,/^# vmstat/p' "$SCRIPT" | sed '$d' >"$SNIPPET"
grep -q 'echo "\[top\]"' "$SNIPPET" || fail "could not cut the [top] block out of $SCRIPT"

cat >"$work/top" <<'EOF'
#!/bin/sh
# Writes one line to the file -f names, as HP-UX top writes its screen.
while [ $# -gt 0 ]; do
	case $1 in
		-f) printf 'top output in %s\n' "$2" >"$2"; shift ;;
	esac
	shift
done
EOF
chmod +x "$work/top"

rc=0
out=$(env -i PATH=/usr/bin:/bin TOP="$work/top" XYMONHOME="$work/home" XYMONTMP="$work/tmp" \
	/bin/sh "$SNIPPET" 2>&1) || rc=$?
assert_equal "[top]
top output in $work/tmp/top.OUT" "$out" "the [top] section (rc $rc)"
[ ! -e "$work/tmp/top.OUT" ] || fail "the top output file was left in XYMONTMP"

pass "the HP-UX client writes top's output to XYMONTMP, prints it and removes it"
