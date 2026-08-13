#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-write-error-backoff.sh
#
# A hostdata write can fail for a reason chkfreespace() cannot see - a
# read-only mount or a permission problem, where free space still looks
# fine. Since a save is only counted against the throttle when it succeeds
# (the ring must mean "real saves"), a persistent write failure would
# otherwise retry and log on every @@clichg. The worker instead backs off
# that host for 5 minutes, so the failure is retried and logged at most
# once per window - and crucially, only that host: other hosts must keep
# saving.
#
# Reproduced by planting a regular file where one host's directory is
# expected: every save under it then fails with ENOTDIR. keepvar is used
# because the blocker has to survive into the run.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

work=$(mktempdir)

setup_xymond_hostdata "$work"
mkdir -p "$work/var/hostdata"
# A regular file where "$CLIENTLOGS/badhost" should be a directory: saving
# "$CLIENTLOGS/badhost/<test>" then fails with ENOTDIR on every message.
printf 'blocker' > "$work/var/hostdata/badhost"

# Interleave five failing messages for badhost with a save for goodhost, so
# the good host lands while badhost is already backed off.
{
	printf '@@clichg#1|1|10.0.0.99|badhost|F1|linux\npayload 1\n@@\n'
	printf '@@clichg#2|1|10.0.0.99|badhost|F2|linux\npayload 2\n@@\n'
	printf '@@clichg#3|1|10.0.0.99|goodhost|G1|linux\npayload g\n@@\n'
	printf '@@clichg#4|1|10.0.0.99|badhost|F3|linux\npayload 3\n@@\n'
	printf '@@clichg#5|1|10.0.0.99|badhost|F4|linux\npayload 4\n@@\n'
} | run_xymond_hostdata_keepvar "$work" --recent-period=1 --recent-count=5 \
	>/dev/null 2>"$work/wl" || true

# The backoff trips exactly once: after the first failure badhost is paused
# for the window, so its later messages neither attempt nor log.
pauses=$(grep -c "Pausing hostdata saves" "$work/wl" || true)
[ "$pauses" -eq 1 ] \
	|| { cat "$work/wl" >&2; fail "expected exactly one save-pause after write errors, got $pauses"; }
errs=$(grep -c "Cannot create file" "$work/wl" || true)
[ "$errs" -le 1 ] \
	|| { cat "$work/wl" >&2; fail "write-error logging not bounded: $errs 'Cannot create file' lines"; }

# The backoff is per-host: goodhost, whose data changed after badhost failed,
# must still be saved. A global backoff would have dropped it.
[ -f "$work/var/hostdata/goodhost/G1" ] \
	|| { cat "$work/wl" >&2; fail "goodhost was suppressed by badhost's write error (backoff not per-host)"; }

echo "OK $(basename "$0")"
