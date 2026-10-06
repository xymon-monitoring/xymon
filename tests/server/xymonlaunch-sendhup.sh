#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonlaunch-sendhup.sh
#
# A HUP to xymonlaunch is a log switch: it reopens its own --log, and relays
# the HUP to each running task marked SENDHUP, so a daemon started from
# tasks.cfg reopens its LOGFILE, which it finds in XYMONLAUNCH_LOGFILENAME.
# Dropping SENDHUP from a task and re-reading stops the relay. A PIDFILE the
# child cannot write is reported, and --dump shows both settings.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONLAUNCH "common/xymonlaunch"

work=$(mktempdir)
cfg="$work/tasks.cfg"
log="$work/launch.log"

# A task that counts the HUPs it gets, one line each in the file named by its
# argument, and says when its trap is set: a HUP before that would kill it.
cat >"$work/hup.sh" <<'EOF'
#!/bin/sh
trap 'echo hup >>"$1"' HUP
touch "$1.ready"
while :; do sleep 1; done
EOF
# A task that prints the log file name the launcher handed it.
cat >"$work/env.sh" <<'EOF'
#!/bin/sh
echo "logfilename=$XYMONLAUNCH_LOGFILENAME"
while :; do sleep 1; done
EOF
chmod +x "$work/hup.sh" "$work/env.sh"

# [a] loses SENDHUP half-way; [b] never has it; [c] always has it, and is how
# the test sees that a HUP pass has happened.
# Every write carries a comment one character longer than the last: the
# launcher re-reads on a change of mtime (whole seconds) or size.
rev=0
write_config() {
	local a_hup=$1
	rev=$((rev + 1))
	{
		printf '#%*s\n' "$rev" ""
		printf '[a]\n\tCMD %s %s\n' "$work/hup.sh" "$work/a"
		[ "$a_hup" = yes ] && printf '\tSENDHUP\n'
		printf '[b]\n\tCMD %s %s\n' "$work/hup.sh" "$work/b"
		printf '[c]\n\tCMD %s %s\n\tSENDHUP\n' "$work/hup.sh" "$work/c"
		printf '[env]\n\tCMD %s\n\tLOGFILE %s\n' "$work/env.sh" "$work/env.log"
		printf '[nopid]\n\tCMD %s %s\n\tPIDFILE %s\n' "$work/hup.sh" "$work/nopid" "$work/absent/nopid.pid"
	} >"$cfg"
}

wait_for() {
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do
		if eval "$@"; then return 0; fi
		sleep 0.2
	done
	return 1
}
hups() { if [ -f "$1" ]; then grep -c hup "$1"; else echo 0; fi; }
# HUP the launcher and wait until [c] has had it relayed.
hup_pass() {
	local n; n=$(hups "$work/c")
	kill -HUP "$launcher"
	wait_for 15 '[ "$(hups "$work/c")" -gt '"$n"' ]' \
		|| fail "a HUP to xymonlaunch never reached the SENDHUP task [c]: $(cat "$log"*)"
	sleep 2		# [a] and [b] got theirs in the same pass, if at all
}

# ---- --dump shows PIDFILE and SENDHUP ----------------------------------------
write_config yes
out=$("$XYMONLAUNCH" --config="$cfg" --dump 2>&1)
assert_contains "PIDFILE $work/absent/nopid.pid" "$out" "--dump shows PIDFILE"
assert_contains "SENDHUP" "$out" "--dump shows SENDHUP"

"$XYMONLAUNCH" --config="$cfg" --log="$log" --no-daemon &
launcher=$!
register_cleanup "kill $launcher 2>/dev/null || true; pkill -f ${work}/ 2>/dev/null || true"

for t in a b c nopid; do
	wait_for 20 '[ -f "$work/$t.ready" ]' || fail "task [$t] never started: $(cat "$log")"
done

# ---- a PIDFILE the child cannot write is reported -----------------------------
wait_for 10 'grep -q "Could not write PID to $work/absent/nopid.pid" "$log"' \
	|| fail "no error for a PIDFILE in a directory that does not exist: $(cat "$log")"

# ---- the task sees its own LOGFILE in XYMONLAUNCH_LOGFILENAME -----------------
wait_for 10 'grep -q "^logfilename=" "$work/env.log" 2>/dev/null' \
	|| fail "task [env] wrote nothing to its LOGFILE"
assert_contains "logfilename=$work/env.log" "$(cat "$work/env.log")" \
	"XYMONLAUNCH_LOGFILENAME names the task's LOGFILE"

# ---- a HUP reopens the launcher's log and reaches only the SENDHUP tasks ------
mv "$log" "$log.old"
hup_pass
wait_for 10 '[ -f "$log" ]' || fail "xymonlaunch did not reopen its --log after a HUP"
[ "$(hups "$work/a")" -eq 1 ] || fail "the SENDHUP task [a] got $(hups "$work/a") HUPs, not 1"
[ "$(hups "$work/b")" -eq 0 ] || fail "task [b], without SENDHUP, got a HUP"

# ---- dropping SENDHUP and re-reading stops the relay --------------------------
# The first HUP re-reads the new file; whether [a] still gets that one depends
# on where in the pass it lands. The second is the one that must not reach it.
write_config no
hup_pass
n=$(hups "$work/a")
hup_pass
[ "$(hups "$work/a")" -eq "$n" ] || fail "task [a] still got a HUP after SENDHUP was removed from it"


pass "xymonlaunch relays a HUP to its SENDHUP tasks only, reopens its log, names each task's LOGFILE to it, and reports a PIDFILE it cannot write"
