#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-sentinel-linux.sh
#
# The remote-df sentinel (#316). With XYMONCLIENT_FS_DF_LOCAL_ONLY=no, df
# stats remote mounts, and a dead NFS/CIFS/Ceph server leaves it in
# uninterruptible D-state where even SIGKILL is ignored -- so a foreground
# `timeout df` cannot help and the whole client cycle hangs until the mount
# recovers. The host then goes purple and *all* metrics are lost, not just
# disk.
#
# The client therefore splits collection: the local set (df -l, which never
# stats a remote server) runs plainly, and the remote set runs behind a
# sentinel that waits a bounded budget and then moves on, leaving the wedged
# df running to be detected next cycle.
#
# Everything here is driven with a df stub and a /proc/mounts fixture: no real
# remote filesystem is involved, and nothing is ever killed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup linux XYMONCLIENT_LINUX

grep -q 'df_sentinel' "$SCRIPT" || fail "remote-df sentinel missing from $SCRIPT (regressed)"

MOUNTS="$TMP/mounts"
DF_CALLS="$TMP/df.calls"; : > "$DF_CALLS"; export DF_CALLS
DF_REMOTE="$TMP/df.remote"; : > "$DF_REMOTE"; export DF_REMOTE
# /proc/filesystems as a real host has it. This matters more than it looks: the
# remote types are all nodev, so the client's default exclusions already contain
# them, and the mount list the sentinel probes is filtered by those exclusions.
# On a stock host DF_LOCAL_ONLY=no therefore probes nothing at all -- surfacing a
# remote filesystem takes INCLUDE_TYPES naming its type as well, which is what
# xymonclient-linux.sh documents. run_cycle passes that recipe; the case at the
# end of this file asserts what happens without it.
printf 'nodev\tproc\nnodev\tsysfs\nnodev\tfuse\nnodev\tnfs\nnodev\tnfs4\nnodev\tcifs\n' \
	> "$TMP/filesystems"

# The stub hangs only when asked about a remote mount, which is how a real df
# wedges: the local set keeps answering throughout.
cat > "$STUB/df" <<'EOF'
#!/bin/sh
echo x >> "$DF_CALLS"
# Model the parts of df's contract this test depends on: -l keeps local
# filesystems only, -x TYPE drops that type, and both filter the mount list
# before anything is stat()ed (which is why neither can hard-block). Named
# mount points report just those. Without -l or names, df lists everything -
# that is the call that has to keep reporting healthy remote filesystems.
_ex=" "; _local_only=; _named=
_prev=
for a in "$@"; do
	case "$_prev" in -x) _ex="$_ex$a " ;; esac
	case "$a" in
		-l) _local_only=1 ;;
		/remote/*) _named="$_named $a" ;;
	esac
	_prev=$a
done
# Remote probes are counted apart from the per-cycle local df, and each records
# its own pid so the test can end the ones it starts.
[ -n "$_named" ] && echo $$ >> "${DF_REMOTE:-/dev/null}"
[ -n "$_named" ] && [ -n "${DF_HANG:-}" ] && exec "$DF_HANGBIN" "${DF_HANGTIME:-120}"
[ -n "${DF_FAIL:-}" ] && exit 1
if [ -n "$_named" ]; then
	# An exclusion applies to an explicitly named operand too, and once every
	# operand is excluded df processes no filesystem and exits nonzero. Not
	# modelling that hid the guarded probe being handed the exclusions for the
	# very types it was asked to report.
	_keep=
	for a in $_named; do
		case "$a" in */sshfs) _t=sshfs ;; *) _t=nfs4 ;; esac
		case "$_ex" in *" $_t "*) ;; *) _keep="$_keep $a" ;; esac
	done
	if [ -z "$_keep" ]; then
		echo "df: no file systems processed" >&2
		exit 1
	fi
	echo "Filesystem 1024-blocks Used Available Capacity Mounted on"
	for a in $_keep; do echo "srv:/exp 100 50 50 50% $a"; done
	exit 0
fi
echo "Filesystem 1024-blocks Used Available Capacity Mounted on"
case "$_ex" in *" ext4 "*) ;; *) echo "/dev/sda1 1000 400 600 40% /" ;; esac
if [ -z "$_local_only" ]; then
	case "$_ex" in *" nfs4 "*) ;; *) echo "srv:/exp 100 50 50 50% /remote/nfs" ;; esac
	case "$_ex" in *" sshfs "*) ;; *) echo "usr@h:/d 200 20 180 10% /remote/sshfs" ;; esac
fi
EOF
chmod +x "$STUB/df"

# The wedge is entered with exec, into a real executable that is itself named
# df, because the guard under test reads the process's command name. A shell
# script does not carry its name into that field portably: Linux sets comm from
# the script, the BSDs report the interpreter, so the same process reads as
# "sh" there and the guard takes the live fixture for a recycled PID
# (@SoundGoof). exec keeps the pid the client recorded, and leaves the wedge a
# single process to clean up.
DF_HANGBIN="$STUB/hang/df"; export DF_HANGBIN
mkdir -p "$STUB/hang"
_sleepbin=$(command -v sleep) || fail "no sleep binary to build the wedge fixture from"
cp "$_sleepbin" "$DF_HANGBIN" && chmod +x "$DF_HANGBIN" || fail "cannot create the wedge fixture"

# Nothing here may outlive the test, including on a failed assertion: fail()
# exits, and every wedge started by then would keep its delay running.
kill_stubs() {
	[ -s "$DF_REMOTE" ] || return 0
	while read -r _p; do [ -n "$_p" ] && kill -9 "$_p" 2>/dev/null; done < "$DF_REMOTE"
	return 0
}
register_cleanup kill_stubs

PS_STUB=

# Extract the [df] block with /proc repointed at the fixtures. The PID-reuse
# guard is exercised for real -- it reads process state through ps, which every
# host this suite runs on has, so nothing here is rewritten for it.
PROC_REWRITE="s#/proc/mounts#$MOUNTS#g; s#/proc/filesystems#$TMP/filesystems#g"
fsf_extract "$TMP/df-section.sh" "$PROC_REWRITE" '\[inode\]'
# The [inode] block runs the sentinel a second time, under its own tag: a wedged
# server therefore leaves TWO probes outstanding per cycle, not one, and the
# unavailable rows have to survive the no-inode-limit drop as well.
fsf_extract "$TMP/df-inode-section.sh" "$PROC_REWRITE"

# end_stub PID : end a wedged df stub. The stub exec'd the wedge into its own
# process, so there is no child to orphan -- the pid is the whole of it.
end_stub() {
	kill -9 "$1" 2>/dev/null || true
}

# run_block SNIPPET [ENV=VAL ...] -- one client cycle over SNIPPET. PS_STUB,
# when set, goes ahead of the fixtures on PATH (see the BSD ps pass at the end). The remote
# types are named in INCLUDE_TYPES because they are nodev and would otherwise be
# excluded before the sentinel ever sees them; that is the documented recipe.
# The caller's assignments come last so a case can override a default (env
# applies them in order, and the last one wins).
run_block() {
	_snip=$1; shift
	env DF_CALLS="$DF_CALLS" \
		DF_REMOTE="$DF_REMOTE" \
		XYMONTMP="$TMP/probe" \
		XYMONCLIENT_FS_DF_LOCAL_ONLY=no \
		XYMONCLIENT_FS_INCLUDE_TYPES="zfs virtiofs tmpfs nfs4 sshfs" \
		XYMONCLIENT_FS_REMOTE_DF_BUDGET=2 \
		PATH="${PS_STUB:+$PS_STUB:}$STUB:$PATH" \
		"$@" \
		/bin/sh "$_snip" 2>/dev/null
}

# run_cycle [ENV=VAL ...] -- one client cycle; prints the [df] block.
run_cycle() { run_block "$TMP/df-section.sh" "$@"; }

# run_full [ENV=VAL ...] -- one client cycle; prints [df] and [inode].
run_full() { run_block "$TMP/df-inode-section.sh" "$@"; }

mkdir -p "$TMP/probe"
local_mounts() { printf '/dev/sda1 / ext4 rw 0 0\nsrv:/exp /remote/nfs nfs4 rw 0 0\nusr@h:/d /remote/sshfs sshfs rw 0 0\n' > "$MOUNTS"; }
local_mounts

# --- healthy -----------------------------------------------------------------
out=$(run_cycle)
assert_contains "/remote/sshfs" "$out" \
	"a remote filesystem whose type is not hard-blocking must still be reported"
assert_contains '/dev/sda1 1000 400 600 40% /' "$out" "the local set must be reported"
assert_contains 'srv:/exp 100 50 50 50% /remote/nfs' "$out" "the remote set must be reported"
# The remote df prints its own header; a second one inside the same [df] block
# is parsed by the server as a filesystem row (xymond_client.c: the header is
# recognised only on the first line), inventing a bogus "Mounted" filesystem.
assert_equal '1' "$(printf '%s\n' "$out" | grep -c '^Filesystem ')" \
	"the remote df header must be stripped: a second one becomes a phantom filesystem"

# --- wedged: bounded, local stays fresh, remote surfaced ----------------------
rm -f "$TMP"/probe/*
out=$(DF_HANG=1 run_cycle DF_HANG=1)
assert_contains '/dev/sda1 1000 400 600 40% /' "$out" \
	"a wedged remote server must not stale or block the local set"
assert_contains 'unavailable:/remote/nfs' "$out" \
	"an unreachable mount must be surfaced, not dropped (the server reads absent as green)"
assert_contains ' 100% /remote/nfs' "$out" "the unavailable row must read as full so the column goes red"
[ -f "$TMP/probe/df-probe-disk.pid" ] || fail "the wedged df must be left recorded as the sentinel"

# --- still wedged: exactly one outstanding df --------------------------------
# Counted from the stub itself: a system-wide process count would be skewed by
# anything else running on the machine. Every count goes through $((...)):
# BSD/macOS wc pads its output with blanks and assert_equal compares strings.
before=$(wc -l < "$DF_CALLS")
out=$(DF_HANG=1 run_cycle DF_HANG=1)
after=$(wc -l < "$DF_CALLS")
assert_equal "$((before + 1))" "$((after))" \
	"while one df is wedged, a cycle must run only the local df -- not a second remote one"
assert_contains 'unavailable:/remote/nfs' "$out" "the mount stays unavailable while wedged"

# --- recovery ----------------------------------------------------------------
# End the wedge and wait for it to actually be gone: while the recorded df is
# still alive the sentinel is *supposed* to keep reporting unavailable, so
# starting the next cycle too early would assert the wrong thing.
wedged=$(cat "$TMP/probe/df-probe-disk.pid")
end_stub "$wedged"
for _ in $(seq 1 100); do
	kill -0 "$wedged" 2>/dev/null || break
	sleep 0.1
done
kill -0 "$wedged" 2>/dev/null && fail "could not end the simulated wedge (pid $wedged)"
out=$(run_cycle)
assert_contains 'srv:/exp 100 50 50 50% /remote/nfs' "$out" \
	"the remote set must return once the server does"
assert_equal '0' "$(find "$TMP/probe" -type f | awk 'END { print NR }')" \
	"the probe files must be cleaned up after a successful cycle"

# --- a type excluded by policy is not probed either --------------------------
# Its "-x" is in the argument list, so naming its mount beside it is the same
# empty-and-nonzero df: the mount must simply not be collected, not collected
# and then reported unavailable.
rm -f "$TMP"/probe/*
out=$(run_cycle XYMONCLIENT_FS_EXCLUDE_TYPES=nfs4)
assert_not_contains 'unavailable:/remote/nfs' "$out" \
	"an excluded type was probed anyway and its healthy mount reported unavailable"
assert_not_contains '/remote/nfs' "$out" "an excluded type must not be reported at all"
assert_contains '/dev/sda1 1000 400 600 40% /' "$out" "the local set must still be reported"
assert_contains '/remote/sshfs' "$out" \
	"excluding one type must not take the other remote filesystems with it"

# --- concurrent cycles claim the probe exactly once --------------------------
# Testing for the pidfile and creating it afterwards let two cycles both find it
# absent and both start a df. Only the last pid written was recorded, so the
# other probe was untracked from then on and more accumulated every cycle --
# the opposite of what the sentinel exists to guarantee.
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"
for _ in $(seq 1 20); do
	run_cycle DF_HANG=1 DF_HANGTIME=5 >/dev/null 2>&1 &
done
wait
assert_equal '1' "$(($(wc -l < "$DF_REMOTE")))" \
	"concurrent cycles each started a remote df; exactly one may own the probe"
while read -r _p; do end_stub "$_p"; done < "$DF_REMOTE"
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"

# --- a claim held by another live cycle is respected -------------------------
# Racing for the window above is probabilistic -- it takes many cycles to land
# in it -- so the state it produces is set up directly instead: a pidfile
# naming a live process that is not a df, which is what a cycle between its
# claim and its fork leaves behind. Without an explicit claim that state does
# not exist, the entry reads as a stale pid, and the second cycle removes it
# and starts a probe of its own on top of the first.
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"
sleep 30 & holder=$!
printf 'claim:%s\n' "$holder" > "$TMP/probe/df-probe-disk.pid"
out=$(run_cycle)
assert_equal '0' "$(($(wc -l < "$DF_REMOTE")))" \
	"a probe already claimed by a live cycle was started a second time"
assert_contains 'unavailable:/remote/nfs' "$out" \
	"a probe claimed by another cycle must report unavailable, not start a second df"
kill "$holder" 2>/dev/null || true; wait "$holder" 2>/dev/null || true
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"

# --- df fails ----------------------------------------------------------------
out=$(DF_FAIL=1 run_cycle DF_FAIL=1)
assert_contains 'unavailable:/remote/nfs' "$out" \
	"a remote df that exits nonzero must report unavailable, not a partial set"

# --- the probe dir is itself on a hard-blocking mount ------------------------
# Nothing may touch it: `test -w` on a wedged mount blocks in D-state, inside
# the fail-safe meant to prevent exactly that.
mkdir -p "$TMP/remoteprobe"
printf '/dev/sda1 / ext4 rw 0 0\nsrv:/exp /remote/nfs nfs4 rw 0 0\nsrv:/p %s nfs4 rw 0 0\n' \
	"$TMP/remoteprobe" > "$MOUNTS"
out=$(run_cycle XYMONTMP="$TMP/remoteprobe" DF_HANG=1)
assert_equal '0' "$(find "$TMP/remoteprobe" -type f | awk 'END { print NR }')" \
	"a probe dir on a hard-blocking filesystem must never be touched"
assert_contains 'unavailable:/remote/nfs' "$out" "the remote set must report unavailable instead"
local_mounts

# --- the default is unchanged ------------------------------------------------
rm -f "$TMP"/probe/*
out=$(env XYMONTMP="$TMP/probe" DF_CALLS="$DF_CALLS" PATH="$STUB:$PATH" /bin/sh "$TMP/df-section.sh" 2>/dev/null)
assert_contains '/dev/sda1 1000 400 600 40% /' "$out" "the default (local-only) report is unchanged"
assert_not_contains '/remote/nfs' "$out" "df -l must still keep remote mounts out by default"
assert_equal '0' "$(find "$TMP/probe" -type f | awk 'END { print NR }')" \
	"the sentinel must not run at all with DF_LOCAL_ONLY=yes"


# --- pidfile unusable --------------------------------------------------------
# The pid is what lets the next cycle recognise a df that is still wedged, so a
# df must not be started when it cannot be recorded - otherwise every cycle
# starts another one, and these are the processes that cannot be killed.
# A directory in the pidfile's place makes creating it fail while the probe
# file next to it stays writable.
mkdir -p "$TMP/probe/df-probe-disk.pid"
_before=$(wc -l < "$DF_CALLS")
pidout=$(run_cycle)
_after=$(wc -l < "$DF_CALLS")
rmdir "$TMP/probe/df-probe-disk.pid"
assert_equal 1 "$((_after - _before))" \
	"an unrecordable pid must stop the remote df from starting (only the plain df may run)"
assert_contains "unavailable:/remote/nfs" "$pidout" \
	"an unrecordable pid must report the remote set unavailable, not drop it"

# --- the inode report is guarded too, under its own tag ----------------------
# [inode] calls run_df a second time, so a dead server leaves TWO probes
# outstanding per cycle -- and the unavailable rows it emits have to survive the
# no-inode-limit drop, which throws away rows whose IUse% column is "-".
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"
out=$(DF_HANG=1 run_full DF_HANG=1)
assert_contains 'unavailable:/remote/nfs' "$(printf '%s\n' "$out" | sed -n '/^\[df\]/,/^\[inode\]/p')" \
	"a wedged server must surface the mount in the disk report"
assert_contains 'unavailable:/remote/nfs' "$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')" \
	"and in the inode report: an absent filesystem reads as green there too"
# The columns, not just the mount name: this row is read positionally, and a
# marker that says 0% used is a green row carrying the word "unavailable".
assert_contains 'unavailable:/remote/nfs 1 1 0 100% /remote/nfs' \
	"$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')" \
	"the inode marker must read as full, or the column stays green"
[ -f "$TMP/probe/df-probe-disk.pid" ] || fail "the disk probe must be recorded"
[ -f "$TMP/probe/df-probe-inode.pid" ] || fail "the inode probe must be recorded under its own tag"
# Two probes, one per tag -- and no third one: a df started outside the sentinel
# is a df nothing will ever recognise, and these cannot be killed.
assert_equal '2' "$(($(wc -l < "$DF_REMOTE")))" \
	"a wedged cycle must start exactly one remote df per report"

# Still wedged: neither report may start a second df. Two local df calls are
# expected -- one per report -- and no remote ones.
before=$(wc -l < "$DF_CALLS")
out=$(DF_HANG=1 run_full DF_HANG=1)
after=$(wc -l < "$DF_CALLS")
assert_equal "$((before + 2))" "$((after))" \
	"while both probes are wedged, a cycle must run only the two local dfs"
for _tag in disk inode; do
	end_stub "$(cat "$TMP/probe/df-probe-$_tag.pid")"
done
rm -f "$TMP"/probe/*; : > "$DF_REMOTE"

# --- a stock host probes nothing at all --------------------------------------
# Every remote type is nodev, so the client's default exclusions already contain
# it and the mount never reaches the sentinel: DF_LOCAL_ONLY=no on its own
# reports no remote filesystem and starts no probe. Surfacing one takes
# INCLUDE_TYPES naming the type as well -- what every case above passes, and
# what xymonclient-linux.sh documents. Getting this wrong in the other
# direction would be worse than useless: a probe of a type the admin excluded
# comes back empty-and-nonzero and reports the mount 100% full.
out=$(env DF_CALLS="$DF_CALLS" DF_REMOTE="$DF_REMOTE" XYMONTMP="$TMP/probe" \
	XYMONCLIENT_FS_DF_LOCAL_ONLY=no XYMONCLIENT_FS_REMOTE_DF_BUDGET=2 \
	DF_HANG=1 PATH="$STUB:$PATH" /bin/sh "$TMP/df-section.sh" 2>/dev/null)
assert_not_contains '/remote/nfs' "$out" \
	"a nodev remote type is excluded before the sentinel sees it, so it is not reported"
assert_equal '0' "$(($(wc -l < "$DF_REMOTE")))" \
	"and no probe is started for it"
assert_equal '0' "$(find "$TMP/probe" -type f | awk 'END { print NR }')" \
	"so no probe state is left behind either"
assert_contains '/remote/sshfs' "$out" \
	"while a remote type that is not nodev is still reported, as DF_LOCAL_ONLY=no asks"

# --- the same wedge, under the BSDs' rule for a process name -----------------
#
# Everything above ran against the host's own ps, so on Linux it says nothing
# about the platforms where this first broke. The two differ in one rule: BSD
# reports the basename of the process's executable, Linux the name it was
# handed, which for a script is the script. Reading /proc/PID/exe applies the
# BSD rule on a Linux host, so the case that failed on four BSDs is exercised
# here rather than only in the VM lanes. Not needed where the host's ps already
# answers that way, and /proc is absent there anyway.
if [ -r /proc/self/exe ]; then
	mkdir -p "$STUB/bsdps"
	cat > "$STUB/bsdps/ps" <<'EOF'
#!/bin/sh
# ps -o comm= -p PID, with the BSDs' answer: the executable's basename.
_pid=; _prev=
for _a in "$@"; do
	case "$_prev" in -p) _pid=$_a ;; esac
	_prev=$_a
done
[ -n "$_pid" ] || exit 1
_exe=$(readlink "/proc/$_pid/exe" 2>/dev/null) || exit 1
[ -n "$_exe" ] || exit 1
echo "${_exe##*/}"
EOF
	chmod +x "$STUB/bsdps/ps"

	while read -r _p; do end_stub "$_p"; done < "$DF_REMOTE"
	rm -f "$TMP"/probe/*; : > "$DF_REMOTE"; : > "$DF_CALLS"

	PS_STUB="$STUB/bsdps"
	out=$(DF_HANG=1 run_cycle DF_HANG=1)
	assert_contains 'unavailable:/remote/nfs' "$out" \
		"the wedge must be surfaced under BSD ps semantics too"
	before=$(wc -l < "$DF_CALLS")
	out=$(DF_HANG=1 run_cycle DF_HANG=1)
	after=$(wc -l < "$DF_CALLS")
	PS_STUB=
	assert_equal "$((before + 1))" "$((after))" \
		"under BSD ps semantics the wedged df went unrecognised and a second remote one started"
fi

pass "the remote-df sentinel bounds a wedged mount, in both reports, without blocking the client"
