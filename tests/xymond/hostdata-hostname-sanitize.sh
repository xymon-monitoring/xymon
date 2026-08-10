#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-hostname-sanitize.sh
#
# The clichg save path builds filesystem paths from the channel-supplied
# host and test names. A legitimate xymon name is a single path component,
# so both are confined with safe_basename(), which rejects any name that
# carries a '/' or is one of the degenerate ".", "..", "" (a bare "..",
# which POSIX basename() passes through unchanged, would otherwise escape
# the CLIENTLOGS tree). A rejected name drops the message; nothing is
# written and nothing outside the tree is touched.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

work=$(mktempdir)

setup_xymond_hostdata "$work"

# assert_rejected <label> -- run one clichg message (on stdin) in a fresh
# tree (run_xymond_hostdata wipes var each call), then assert nothing was
# written: the only paths under var are the empty hostdata directory
# itself. A confined name that is rejected saves nothing and creates no
# directory, inside the tree or above it.
assert_rejected() {
	local label=$1 tree
	run_xymond_hostdata "$work" >/dev/null 2>>"$work/worker.log" || true
	tree=$(find "$work/var" | sort)
	[ "$tree" = "$(printf '%s\n%s' "$work/var" "$work/var/hostdata")" ] \
		|| { echo "$tree" >&2; fail "$label was not rejected cleanly"; }
}

# Positive control: an ordinary host/test pair is saved under the tree.
printf '@@clichg#0|1|10.0.0.99|realhost|F0|linux\npayload\n@@\n' |
	run_xymond_hostdata "$work" >/dev/null 2>>"$work/worker.log" || true
[ -f "$work/var/hostdata/realhost/F0" ] \
	|| { cat "$work/worker.log" >&2; fail "an ordinary hostname was not saved"; }

# A '/'-bearing hostname: safe_basename() rejects it outright rather than
# trimming to a component, so "../escapee" is neither saved as "escapee"
# nor allowed to escape the tree.
printf '@@clichg#1|1|10.0.0.99|../escapee|F1|linux\npayload\n@@\n' | assert_rejected "hostname '../escapee'"

# A '/'-bearing test name is rejected the same way.
printf '@@clichg#2|1|10.0.0.99|realhost|../../../escape2|linux\npayload\n@@\n' | assert_rejected "testname '../../../escape2'"

# A bare ".." hostname: POSIX basename("..") is ".." unchanged, which would
# make the save path "$CLIENTLOGS/../<testname>" and drop a file in the
# parent of the hostdata tree. safe_basename() rejects it.
printf '@@clichg#3|1|10.0.0.99|..|F3|linux\npayload\n@@\n' | assert_rejected "bare '..' hostname"
[ ! -e "$work/var/F3" ] \
	|| fail "bare '..' hostname wrote a file into the parent of the hostdata tree"

echo "OK $(basename "$0")"
