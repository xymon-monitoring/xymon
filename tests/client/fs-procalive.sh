#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-procalive.sh
#
# The remote-df sentinel asks fs_procalive() whether the df an earlier cycle
# recorded still runs. kill -0 cannot tell it: it answers for a zombie too,
# a process that has exited and stays in the table until its parent reaps
# it. The recorded df is an orphan, reaped by PID 1 -- and where PID 1 is not
# an init, a container or the unshare testbed of #866, by nothing. Read as a
# df still wedged, the zombie kept the mount unavailable for as long as it
# lasted, which is for good.
#
# The sentinel's tests replace fs_procalive() with a stub, so its own body is
# covered by this test alone, on the OS it is written for:
#
#   Linux        /proc/PID/stat holds the state after the name, Z for a zombie
#   BSD, macOS   ps -o stat= prints it, Z for a zombie
#
# It runs the client's own helper, extracted, against a process that runs, a
# zombie -- a child whose parent replaces itself with a sleep, which never
# waits for it -- and a pid that is gone.
#
# native-primitive: fs_procalive

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

os=$(uname -s)
case "$os" in
  Linux)   client=linux ;;
  FreeBSD) client=freebsd ;;
  NetBSD)  client=netbsd ;;
  OpenBSD) client=openbsd ;;
  Darwin)  client=darwin ;;
  *) skip "no recorded process-state rule for $os" ;;
esac

script="$ROOT/client/xymonclient-$client.sh"
[ -f "$script" ] || skip "$script missing"
grep -q '^fs_procalive()' "$script" \
	|| fail "$client: fs_procalive() missing from the client this host runs"

case "$os" in
  Linux)
	[ -r /proc/self/stat ] || skip "no /proc on this Linux, so its client can only ask kill -0"
	;;
  *)
	ps -o stat= -p $$ > /dev/null 2>&1 || skip "no ps that tells a process's state, so this OS's client can only ask kill -0"
	;;
esac

work=$(mktempdir)

# The client's helper, run alone, by the shell the client runs under.
{ sed -n '/^fs_procalive()/,/^}/p' "$script"; printf 'fs_procalive "$1"\n'; } > "$work/procalive.sh"
procalive() { sh "$work/procalive.sh" "$1"; }

# The zombie, and its parent: the sleep that never waits for it. The child
# exits only once its parent has become that sleep: exiting at once, it could
# be reaped by its shell before the exec, and there was no zombie. Its parent's
# command comes from ps, or from /proc where there is no ps; comm is matched as
# *sleep, as macOS gives the whole path, and the wait is bounded.
sh -c "sh -c 'n=0; while [ \$n -lt 200 ]; do c=\$(ps -o comm= -p \$PPID 2>/dev/null || cat /proc/\$PPID/comm 2>/dev/null); case \$c in *sleep) break ;; esac; sleep 0.05; n=\$((n + 1)); done; exit 0' & echo \$! >'$work/zombie'; exec sleep 30" &
holder=$!
register_cleanup "stop_process -KILL $holder"
for _ in $(seq 1 50); do
	[ -s "$work/zombie" ] && break
	sleep 0.1
done
[ -s "$work/zombie" ] || fail "the fixture never wrote the pid of its child"
zombie=$(cat "$work/zombie")
# Polled until the state says Z, not given a fixed time.
zstate() {
	ps -o stat= -p "$1" 2>/dev/null && return
	read -r _l < "/proc/$1/stat" 2>/dev/null && _l=${_l##*") "} && printf '%s\n' "${_l%% *}"
}
for _ in $(seq 1 150); do
	case $(zstate "$zombie") in *Z*) break ;; esac
	sleep 0.1
done
kill -0 "$zombie" 2>/dev/null || fail "the exited process is not in the table: no zombie to check with"

procalive "$holder" || fail "$client: fs_procalive() says a running process has gone"
procalive $$ || fail "$client: fs_procalive() says this shell has gone"
procalive "$zombie" \
	&& fail "$client: fs_procalive() takes a process that has exited, and that nothing reaped, for a running one"

# Reaped: its parent ends, and the zombie goes to PID 1 -- or stays, where
# PID 1 does not reap. Gone either way.
stop_process -KILL "$holder"
procalive "$zombie" && fail "$client: fs_procalive() takes an exited process for a running one, once its parent has ended"
procalive "$holder" && fail "$client: fs_procalive() says a pid that is gone still runs"

pass "$client: fs_procalive() tells a running process from one that has exited, reaped or not"
