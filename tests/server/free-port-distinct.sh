#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/free-port-distinct.sh
#
# free_port (tests/lib/xymond-daemon.sh) never gives one test the same port
# twice. Each call draws at random from a window of a few thousand ports, and
# nothing listens on a port it returned until the caller binds it, so two
# calls could return the same number: tests/server/xymond-multi-listen.sh then
# asked xymond to listen twice on one port and failed with "Address in use".
#
# Five hundred draws in a window of 8192 hold a repeat almost surely when
# nothing prevents it (the chance of none is about e^-15), so this fails
# without the record of ports already given, and passes with it.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMONCLIENT common/xymon

work=$(mktempdir)
: > "$work/ports"
for _ in $(seq 1 500); do
	p=$(free_port) || fail "free_port found no port after $(wc -l < "$work/ports" | tr -d ' ') draws"
	printf '%s\n' "$p" >> "$work/ports"
done

repeats=$(sort "$work/ports" | uniq -d | tr '\n' ' ')
[ -z "$repeats" ] || fail "free_port gave this test the same port more than once: $repeats"

pass "free_port gives a test 500 different ports, never one it has already given"
