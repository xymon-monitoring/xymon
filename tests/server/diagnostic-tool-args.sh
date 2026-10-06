#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/diagnostic-tool-args.sh
#
# The diagnostics "make install-tools" installs -- lib/'s xymonloadhosts,
# xymonstackio, xymonavailability and xymonlocator, and xymonnet's
# xymonnetprobe -- must
# answer --help with their usage and exit 0, and must refuse arguments
# they cannot use with the usage and exit 1, not crash. They used to:
#
#   - xymonloadhosts and xymonstackio with no arguments read argv[1] and segfaulted;
#   - xymonavailability on a file name with no dot passed strrchr's NULL on;
#   - xymonstackio on a file it could not open retried it forever, without
#     reading its next command;
#   - --help was taken by xymonstackio for a file name and by xymonlocator
#     for an address, and xymonnetprobe with no test ran nothing and exited 0.
#
# Needs a server build: the tools are built by lib/ and xymonnet's "all".

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

require_bin LOADHOSTS lib/xymonloadhosts
require_bin STACKIO lib/xymonstackio
require_bin AVAILABILITY lib/xymonavailability
require_bin LOCATOR lib/xymonlocator
require_bin CONTEST xymonnet/xymonnetprobe

work=$(mktempdir); register_cleanup "rm -rf '$work'"

# expect_status <want> <what> <cmd...> -- runs cmd with stdin from /dev/null,
# output in $work/out and $work/err; fails unless it exits with <want>.
expect_status() {
	local want=$1 what=$2 rc=0; shift 2
	"$@" </dev/null >"$work/out" 2>"$work/err" || rc=$?
	[ "$rc" -le 128 ] || fail "$what: killed by signal $((rc - 128)) -- a crash, expected exit $want"
	[ "$rc" -eq "$want" ] || fail "$what: exit $rc, expected $want
stdout: $(cat "$work/out")
stderr: $(cat "$work/err")"
}

# ---- --help prints the usage on stdout and exits 0 -------------------------
for v in LOADHOSTS STACKIO AVAILABILITY LOCATOR CONTEST; do
	bin=${!v}
	expect_status 0 "$(basename "$bin") --help" "$bin" --help
	grep -q '^Usage: ' "$work/out" \
		|| fail "$(basename "$bin") --help printed no usage on stdout: $(cat "$work/out")"
done

# ---- unusable arguments print the usage on stderr and exit 1 ----------------
# expect_refusal <what> <cmd...>
expect_refusal() {
	local what=$1; shift
	expect_status 1 "$what" "$@"
	grep -q '^Usage: ' "$work/err" || fail "$what: refused without the usage on stderr: $(cat "$work/err")"
}

expect_refusal "xymonloadhosts with no arguments" "$LOADHOSTS"
expect_refusal "xymonloadhosts with a hosts file and no host" "$LOADHOSTS" "$work/hosts.cfg"
expect_refusal "xymonstackio with no arguments" "$STACKIO"
expect_refusal "xymonlocator with no arguments" "$LOCATOR"
expect_refusal "xymonnetprobe with no test" "$CONTEST" --timeout=1

# A history file is HOST.SERVICE; the dot that matters is in the file name,
# not in a directory above it.
mkdir -p "$work/hist.d"
: >"$work/hist.d/nodot"
expect_refusal "xymonavailability on a history file name without a service" \
	"$AVAILABILITY" "$work/hist.d/nodot" 0 1

# ---- xymonstackio on a missing file reports it once and reads its next command ---
# A loop that never reads stdin floods its output: a small file-size cap
# stops it there, where the fixed tool writes two short lines.
( ulimit -f 128; printf '.\n' | "$STACKIO" "$work/missing.cfg" >"$work/missing.out" 2>&1 ) || :
lines=$(grep -c 'Cannot open file' "$work/missing.out" || :)
[ "$lines" -eq 1 ] || fail "xymonstackio reported the missing file $lines times, expected once -- it retries the file instead of reading its next command"

pass "the install-tools diagnostics answer --help, and refuse unusable arguments with their usage instead of crashing"
