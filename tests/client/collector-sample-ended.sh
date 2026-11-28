#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/collector-sample-ended.sh
#
# Every client script that has a vmstat -- and SunOS's two iostat -- starts it
# detached, for a sample that takes its whole interval, and the next run
# reports the file it leaves. What the collector prints at once is not the
# sample; that comes when the interval is up. A collector ended before then --
# xymonlaunch ends what its periodic tasks left when it stops, and so does a
# shutdown -- has printed the first part only. Reported, the first part is
# taken for the sample.
#
# So each of those lines publishes its file only when it was not ended, and
# removes it otherwise:
#
#   - the collector runs to its end: its output becomes xymon_<what>.<host>,
#     and the temporary file is gone;
#   - the collector and the shell waiting on it are both ended, as a signal to
#     their process group does: no xymon_<what>.<host> appears, and the
#     temporary file is gone too, so stops do not pile them up. That holds when
#     the signal comes a second time, while the file is being removed.
#
# Each line is extracted from its script and run alone, with a stub collector
# first in PATH: a whole script would need a stub for every tool it calls, and
# six of the seven cannot run here at all. The same line is in every script,
# which is what lets one host test them all.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/bin"

# The stub prints what a collector prints at once, then either finishes with
# the sample or waits to be ended. It records its own pid and the pid of the
# shell that started it -- the two a group signal reaches -- and execs its
# wait, so the first is the one the signal has to end.
cat > "$work/bin/collector" <<'STUB'
#!/bin/sh
echo "header"
echo "since-boot"
if [ -f "$STUBDIR/finish" ]; then
	echo "the-sample"
	exit 0
fi
echo "$$ $PPID" > "$STUBDIR/pids"
exec sleep 60
STUB
chmod +x "$work/bin/collector"
ln -s collector "$work/bin/vmstat"
ln -s collector "$work/bin/iostat"

# A stop seldom signals once: systemd signals the whole service and then
# xymonlaunch signals the task's group, and a shutdown does much the same. The
# second signal finds rm running. This rm sends itself that signal before it
# removes anything, so the file goes only when the shell ignored the signal
# before starting it.
cat > "$work/bin/rm" <<'STUB'
#!/bin/sh
kill -TERM $$
exec /bin/rm "$@"
STUB
chmod +x "$work/bin/rm"

# Up to ten seconds for a background shell to do its one step.
wait_for() {	# $1 = a shell condition
	local n=0
	while ! eval "$1"; do
		n=$((n + 1))
		[ "$n" -lt 50 ] || return 1
		sleep 0.2
	done
}

published() { find "$tmp" -type f -name 'xymon_*.fl.test' | wc -l | tr -d ' '; }
temporaries() { find "$tmp" -type f -name 'xymon_*.fl.test.*' | wc -l | tr -d ' '; }

run_line() {
	env PATH="$work/bin:$PATH" STUBDIR="$work" \
		XYMONTMP="$tmp" MACHINEDOTS=fl.test sh "$work/snippet.sh"
}

checked=0
for script in "$ROOT"/client/xymonclient-*.sh; do
	name=${script##*/}
	lines=$(grep '^nohup sh -c "' "$script" || true)
	[ -n "$lines" ] || continue

	while IFS= read -r line; do
		what=$(printf '%s\n' "$line" | sed 's/^nohup sh -c "ended=; trap [^;]*; //; s/ 1>.*//')
		tmp="$work/tmp.$checked"; mkdir -p "$tmp"
		printf '%s\n' "$line" > "$work/snippet.sh"

		# --- the collector runs to its end: the sample is published ----------
		: > "$work/finish"
		run_line
		wait_for '[ "$(published)" = 1 ]' \
			|| fail "$name, '$what': a collector that finished left nothing for the next run to report"
		assert_contains "the-sample" "$(cat "$tmp"/xymon_*.fl.test)" \
			"$name, '$what': the published file does not hold the sample"
		wait_for '[ "$(temporaries)" = 0 ]' \
			|| fail "$name, '$what': a collector that finished left its temporary file behind"

		# --- collector and shell are ended: nothing published, nothing left --
		rm -f "$tmp"/xymon_* "$work/finish" "$work/pids"
		run_line
		wait_for '[ -s "$work/pids" ]' \
			|| fail "$name, '$what': the stub collector did not start"
		wait_for '[ "$(temporaries)" = 1 ]' \
			|| fail "$name, '$what': the output is not being written to a temporary file"
		# shellcheck disable=SC2046  # two pids, on purpose
		kill -TERM $(cat "$work/pids")
		wait_for '[ "$(temporaries)" = 0 ]' \
			|| fail "$name, '$what': a collector that was ended left its temporary file behind"
		[ "$(published)" = 0 ] \
			|| fail "$name, '$what': a collector that was ended was published, its first lines reported as the sample ($(cat "$tmp"/xymon_*.fl.test | tr '\n' '|'))"

		checked=$((checked + 1))
	done <<LINES
$lines
LINES
done

# Every detached line in the client scripts, not only those the loop knew how
# to read: one written another way would otherwise go untested, unnoticed.
detached=$(cat "$ROOT"/client/xymonclient-*.sh | grep -c '^nohup ' || true)
[ "$checked" -gt 0 ] && [ "$checked" = "$detached" ] \
	|| fail "the client scripts detach $detached commands with nohup, and $checked of them were tested"

pass "each of the $checked detached collectors publishes its sample when it finished, and removes it when it was ended"
