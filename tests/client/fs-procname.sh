#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-procname.sh
#
# The suite runs every client's [df] block with its OS primitives replaced,
# which is what lets one lane verify all five. A stub is only as good as the
# assumption behind it, and fs_procname() rests on one per platform:
#
#   Linux        /proc/PID/comm holds the name of the running executable
#   BSD, macOS   ps -o comm= prints it, with a leading path on macOS
#
# Nothing else checks those, so if either changed, every stubbed test would
# stay green while the PID-reuse guard broke in production. This one runs the
# client's own helper -- extracted, not reimplemented, or a client that started
# asking ps for the wrong thing would still pass here -- against a process
# whose name it knows, and skips on any OS whose rule is not recorded.
#
# native-primitive: fs_procname

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"	# fsf_wedge_binary

ROOT=$(find_root)

os=$(uname -s)
case "$os" in
  Linux)   client=linux ;;
  FreeBSD) client=freebsd ;;
  NetBSD)  client=netbsd ;;
  OpenBSD) client=openbsd ;;
  Darwin)  client=darwin ;;
  *) skip "no recorded process-name rule for $os" ;;
esac

script="$ROOT/client/xymonclient-$client.sh"
[ -f "$script" ] || skip "$script missing"
grep -q '^fs_procname()' "$script" \
	|| fail "$client: fs_procname() missing from the client this host runs"

case "$os" in
  Linux)
	[ -r /proc/self/comm ] || skip "no /proc on this Linux, so its client cannot use it either"
	;;
  *)
	command -v ps > /dev/null 2>&1 || skip "no ps, which is what this OS's client reads"
	;;
esac

work=$(mktempdir)

# The client's helper, run alone. Extracting it is the point: a reimplementation
# here would test this file's idea of the primitive, not the client's.
{ sed -n '/^fs_procname()/,/^}/p' "$script"; printf 'fs_procname "$1"\n'; } > "$work/procname.sh"
procname() { sh "$work/procname.sh" "$1"; }

# A process named "df" that does nothing but wait -- the shape of the wedged df
# the guard exists for, and the shape its fixtures imitate. Built rather than
# copied: this test asks what ps says about a binary called df, and a copied
# /bin/sleep is not reliably one. On Alpine it is BusyBox, which reads its
# applet from argv[0] and so runs df and exits.
fsf_wedge_binary "$work/df"
"$work/df" 30 &
pid=$!
# Off the shell's job table, or reaping it prints "Killed" into the suite's
# output, which reads like a failure and is not one.
disown "$pid" 2>/dev/null || true
register_cleanup "kill -9 $pid 2>/dev/null || true"

# Wait for the exec, not for a duration: until then the pid is still the forking
# shell and would answer with the shell's name.
i=0
while :; do
	kill -0 "$pid" 2>/dev/null || fail "the fixture exited before it could be inspected"
	case "$(procname "$pid")" in
	  *df) break ;;
	esac
	i=$((i + 1))
	[ "$i" -lt 100 ] || fail "$client: fs_procname() never named the fixture after 10s -- the primitive it reads has changed"
	sleep 0.1
done

name=$(procname "$pid")
[ -n "$name" ] || fail "$client: fs_procname() returned nothing for a running process"

# The caller strips the path and compares to "df" (see df_sentinel), so that is
# what is checked here.
assert_equal "df" "${name##*/}" \
	"$client: fs_procname() names a running process after its executable"

case "$os" in
  Linux)
	# No path, no arguments -- and no external command: minimal images ship
	# without ps, which is why the Linux client reads /proc itself.
	assert_equal "df" "$name" \
		"Linux: the name comes back bare, with no path and no arguments"
	;;
  Darwin)
	# macOS prints the full path where the BSDs print the bare name. Both are
	# handled, but pin which is which: if macOS ever switched, the caller's
	# ${_c##*/} would go from load-bearing to dead and nothing would say so.
	case "$name" in
	  /*) ;;
	  *) fail "macOS used to print the full path in comm; it printed '$name'" ;;
	esac
	;;
esac

# A pid that is gone must answer empty rather than something the caller would
# mistake for a name: that is what makes an unreadable name distinguishable
# from a finished probe.
kill -9 "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
dead=$(procname "$pid")
assert_equal "" "$dead" \
	"$client: fs_procname() returns nothing for a pid that no longer exists"

pass "$client: fs_procname() answers as this OS's client assumes"
