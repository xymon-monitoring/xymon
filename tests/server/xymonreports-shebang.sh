#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonreports-shebang.sh
#
# xymonreports.sh is a bash script and finds bash through PATH, so it starts
# wherever bash is installed. Its first line named /bin/bash, which FreeBSD,
# OpenBSD and NetBSD do not have (bash is a package there, in /usr/local/bin
# or /usr/pkg/bin): the installed script, and the periodic-report tasks that
# run it, failed there before reading a line, with "No such file or directory"
# about the script itself.
#
# Checked by running the script directly, as xymonlaunch does, with a stand-in
# bash first on PATH that leaves a mark and hands over to the real one: the
# mark is there only if the script asked PATH for bash.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_cfg REPORTS_DIST "xymond/xymonreports.sh.DIST"

work=$(mktempdir)
mkdir -p "$work/bin"
sed -e "s!@XYMONHOME@!$work!g" "$REPORTS_DIST" >"$work/xymonreports.sh"
chmod 755 "$work/xymonreports.sh"

cat >"$work/bin/bash" <<STANDIN
#!/bin/sh
: >"$work/found-through-path"
exec "$BASH" "\$@"
STANDIN
chmod 755 "$work/bin/bash"

status=0
out=$(PATH="$work/bin:/bin:/usr/bin" "$work/xymonreports.sh" --help 2>&1) || status=$?

[ -f "$work/found-through-path" ] \
	|| fail "xymonreports.sh did not find bash through PATH (exit $status), so it cannot start where bash is not /bin/bash: $out"
assert_contains "Usage:" "$out" \
	"xymonreports.sh started through PATH's bash but did not print its usage"

pass "xymonreports.sh finds bash through PATH, so it starts where bash is not /bin/bash"
