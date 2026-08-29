#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-rename-throttle-reset.sh
#
# @@renamehost must drop the old name's throttle bookkeeping, not just move
# its directory. The savetimes entry (hostname + tstamp ring) is per-process
# state; if it survives the rename it both leaks and keeps the old name's
# throttle budget, so a host that later reappears under the old name stays
# throttled against saves it no longer has on disk.
#
# renamehost is used rather than drophost because drophost removes the
# directory with a backgrounded fork (dropdirectory(dir,1)); re-saving the
# same host immediately would race that child's rmdir. rename() is a plain
# syscall and exercises the identical drop_savetimes() path deterministically.
#
# The whole exchange goes through one process in a single stream, because
# the save-time state lives in per-process memory.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

work=$(mktempdir)

setup_xymond_hostdata "$work"

# --recent-count=1 in a long window: A/F1 saves and fills A's budget, A/F2
# is throttled; renaming A->B moves the directory and must clear A's budget;
# a later A/F3 must save because A's throttle state is gone.
{
	printf '@@clichg#1|1|10.0.0.99|hostA|F1|linux\npayload 1\n@@\n'
	printf '@@clichg#2|1|10.0.0.99|hostA|F2|linux\npayload 2\n@@\n'
	printf '@@renamehost|1|10.0.0.99|hostA|hostB\n@@\n'
	printf '@@clichg#3|1|10.0.0.99|hostA|F3|linux\npayload 3\n@@\n'
} | run_xymond_hostdata "$work" --recent-period=3600 --recent-count=1 \
	>/dev/null 2>>"$work/worker.log" || true

# The rename moved A's single saved file under B.
[ -f "$work/var/hostdata/hostB/F1" ] \
	|| { cat "$work/worker.log" >&2; fail "renamehost did not move the hostdata directory"; }
# F2 was throttled, so it never existed under either name.
[ ! -e "$work/var/hostdata/hostA/F2" ] && [ ! -e "$work/var/hostdata/hostB/F2" ] \
	|| fail "F2 was saved despite the budget being full"
# The fix: A's throttle entry was dropped by the rename, so F3 saves again.
# Without it, A's ring is still full within the window and F3 is dropped.
[ -f "$work/var/hostdata/hostA/F3" ] \
	|| { cat "$work/worker.log" >&2; fail "throttle budget not reset by renamehost: F3 for the reused old name was dropped"; }

echo "OK $(basename "$0")"
