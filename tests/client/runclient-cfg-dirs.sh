#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/runclient-cfg-dirs.sh
#
# runclient.sh takes the client's log and tmp directories from
# xymonclient.cfg, XYMONCLIENTLOGS and XYMONTMP, expanded as the client
# programs expand them. It checks that both are writable before starting,
# and keeps clientlaunch.log and clientlaunch.<host>.pid in the log
# directory. It used XYMONCLIENTHOME/logs and XYMONCLIENTHOME/tmp whatever
# the file said, so a client whose file names other directories would not
# start without a link left at each of those two paths.
#
# Each case is a scratch XYMONCLIENTHOME holding runclient.sh, the built
# xymoncmd, and a stub xymonlaunch that records its arguments and exits:
# no client is started. Pinned here:
#   - a file naming other directories, through $XYMONHOME: start checks
#     those, and gives xymonlaunch a --log and --pidfile in the log one;
#     status reads the pidfile there;
#   - with a pidfile there: stop signals its pid, and start finds that
#     client running, stops it and removes the file;
#   - either named directory missing: start refuses, naming it;
#   - the shipped xymonclient.cfg: the directories the client programs read
#     from it, which name the directories the build installed;
#   - a file without the two lines: the paths under XYMONCLIENTHOME;
#   - a log directory set in the file's xymonclient.d: that one, over the
#     file's own line.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

default="common/xymoncmd"
if [ -z "${XYMONCMD:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymoncmd" ]; then
	default="client/xymoncmd"
fi
require_bin XYMONCMD "$default"

ROOT=$(find_root)
work=$(mktempdir)
mkdir "$work/cfg"
host=runclient.example

# mkhome NAME CFGFILE -- a client home whose etc/xymonclient.cfg is CFGFILE.
mkhome() {
	local home="$work/$1"
	mkdir -p "$home/bin" "$home/etc"
	cp "$ROOT/client/runclient.sh" "$home/runclient.sh"
	cp "$XYMONCMD" "$home/bin/xymoncmd"
	cat >"$home/bin/xymonlaunch" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" >"$XYMONCLIENTHOME/launch.args"
EOF
	chmod +x "$home/bin/xymonlaunch"
	cp "$2" "$home/etc/xymonclient.cfg"
	printf '%s\n' "$home"
}

# run HOME CMD -- runclient.sh in an environment that sets none of the
# client's variables, so that only the file can.
run() { env -i PATH=/usr/bin:/bin "$1/runclient.sh" --hostname="$host" "$2" 2>&1; }

# A file naming other directories.
cat >"$work/cfg/moved.cfg" <<'EOF'
XYMONHOME="$XYMONCLIENTHOME"
XYMONTMP="$XYMONHOME/var/tmp"
XYMONCLIENTLOGS="$XYMONHOME/var/log"
EOF
home=$(mkhome moved "$work/cfg/moved.cfg")

rc=0; out=$(run "$home" start) || rc=$?
assert_equal "Cannot write to the $home/var/log directory" "$out" \
	"start with the named log directory missing (rc $rc)"
mkdir -p "$home/var/log"
rc=0; out=$(run "$home" start) || rc=$?
assert_equal "Cannot write to the $home/var/tmp directory" "$out" \
	"start with the named tmp directory missing (rc $rc)"
mkdir -p "$home/var/tmp"

rc=0; out=$(run "$home" start) || rc=$?
grep -q "started on $host" <<<"$out" || fail "start with the named directories present (rc $rc): $out"
grep -qx -- "--log=$home/var/log/clientlaunch.log" "$home/launch.args" \
	|| fail "xymonlaunch was not given the named log directory: $(cat "$home/launch.args")"
grep -qx -- "--pidfile=$home/var/log/clientlaunch.$host.pid" "$home/launch.args" \
	|| fail "xymonlaunch was not given its pidfile in the named log directory: $(cat "$home/launch.args")"

# status reads that pidfile: this shell's pid stands for a running launcher.
echo $$ >"$home/var/log/clientlaunch.$host.pid"
assert_equal "Xymon client (clientlaunch) running with PID $$" "$(run "$home" status)" \
	"status did not read the pidfile in the named log directory"

# start finds a client already running by that pidfile, stops it and removes
# the file; stop signals the pid it holds. Both read the named directory, not
# XYMONCLIENTHOME/logs.
sleep 30 & victim=$!
register_cleanup "stop_process $victim"
echo "$victim" >"$home/var/log/clientlaunch.$host.pid"
out=$(run "$home" stop)
assert_equal "Xymon client stopped" "$out" "stop did not signal the pid in the named log directory"
{ wait "$victim"; } 2>/dev/null || true
if kill -0 "$victim" 2>/dev/null; then kill "$victim"; fail "stop left the process the named log directory's pidfile holds running"; fi
sleep 30 & victim=$!
register_cleanup "stop_process $victim"
echo "$victim" >"$home/var/log/clientlaunch.$host.pid"
out=$(run "$home" start)
grep -q "already running" <<<"$out" || fail "start did not find the client its pidfile in the named log directory records: $out"
{ wait "$victim"; } 2>/dev/null || true
if kill -0 "$victim" 2>/dev/null; then kill "$victim"; fail "start did not stop the client its pidfile in the named log directory records"; fi
[ ! -e "$home/var/log/clientlaunch.$host.pid" ] || fail "start left the stale pidfile in the named log directory"

# dirs HOME VAR -- what the client programs make of VAR in HOME's
# xymonclient.cfg, asked of the built xymoncmd as they would.
dirs() {
	env -i PATH=/usr/bin:/bin XYMONCLIENTHOME="$1" \
		"$1/bin/xymoncmd" --env="$1/etc/xymonclient.cfg" printf '%s' "\$$2" 2>/dev/null
}

# The shipped file names the directories the build installed:
# XYMONHOME/logs and /tmp unless INSTALLCLIENT{LOG,TMP}DIR, or a client-only
# build's INSTALLTMPDIR, moved one. runclient.sh must use what the client
# programs read from it, whichever this build is: started there when both
# are under the scratch home, refused naming the directory when it is
# elsewhere and cannot be written to.
name=xymonclient.cfg.DIST
home=$(mkhome "$name" "$ROOT/client/xymonclient.cfg.DIST")
logdir=$(dirs "$home" XYMONCLIENTLOGS); tmpdir=$(dirs "$home" XYMONTMP)
[ -n "$logdir" ] && [ -n "$tmpdir" ] || fail "xymoncmd gives no XYMONCLIENTLOGS or XYMONTMP for the shipped file: '$logdir' '$tmpdir'"
for d in "$logdir" "$tmpdir"; do
	case $d in "$home"/*) mkdir -p "$d" ;; esac
done
rc=0; out=$(run "$home" start) || rc=$?
if [ -w "$logdir" ] && [ -w "$tmpdir" ]; then
	grep -q "started on $host" <<<"$out" || fail "start with $name (rc $rc): $out"
	grep -qx -- "--log=$logdir/clientlaunch.log" "$home/launch.args" \
		|| fail "with $name, the log is not in $logdir, where the client logs: $(cat "$home/launch.args")"
	grep -qx -- "--pidfile=$logdir/clientlaunch.$host.pid" "$home/launch.args" \
		|| fail "with $name, the pidfile is not in $logdir: $(cat "$home/launch.args")"
elif [ ! -w "$logdir" ]; then
	assert_equal "Cannot write to the $logdir directory" "$out" \
		"with $name, start did not check the log directory the client uses (rc $rc)"
else
	assert_equal "Cannot write to the $tmpdir directory" "$out" \
		"with $name, start did not check the tmp directory the client uses (rc $rc)"
fi

# A file without the two lines: the paths under the home, as before.
printf 'XYMONHOME="$XYMONCLIENTHOME"\n' >"$work/cfg/bare.cfg"
name=bare.cfg
home=$(mkhome "$name" "$work/cfg/bare.cfg")
mkdir -p "$home/logs" "$home/tmp"
rc=0; out=$(run "$home" start) || rc=$?
grep -q "started on $host" <<<"$out" || fail "start with $name (rc $rc): $out"
grep -qx -- "--log=$home/logs/clientlaunch.log" "$home/launch.args" \
	|| fail "with $name, the log is not under XYMONCLIENTHOME/logs: $(cat "$home/launch.args")"
grep -qx -- "--pidfile=$home/logs/clientlaunch.$host.pid" "$home/launch.args" \
	|| fail "with $name, the pidfile is not under XYMONCLIENTHOME/logs: $(cat "$home/launch.args")"
rmdir "$home/tmp"
rc=0; out=$(run "$home" start) || rc=$?
assert_equal "Cannot write to the $home/tmp directory" "$out" \
	"with $name, start did not check XYMONCLIENTHOME/tmp (rc $rc)"

# A directory set in the xymonclient.d drop-in directory, which the file
# includes after its own settings: where an administrator's change goes,
# since install never writes there. It wins over the file's own line.
name=dropin.cfg
home=$(mkhome "$name" "$work/cfg/bare.cfg")
mkdir -p "$home/etc/xymonclient.d" "$home/dlog" "$home/tmp"
cat >"$home/etc/xymonclient.cfg" <<EOF
XYMONHOME="\$XYMONCLIENTHOME"
XYMONTMP="\$XYMONHOME/tmp"
XYMONCLIENTLOGS="\$XYMONHOME/logs"
optional directory $home/etc/xymonclient.d
EOF
echo 'XYMONCLIENTLOGS="$XYMONHOME/dlog"' >"$home/etc/xymonclient.d/local.cfg"
rc=0; out=$(run "$home" start) || rc=$?
grep -q "started on $host" <<<"$out" || fail "start with $name (rc $rc): $out"
grep -qx -- "--log=$home/dlog/clientlaunch.log" "$home/launch.args" \
	|| fail "with $name, the log is not in the directory xymonclient.d names: $(cat "$home/launch.args")"
grep -qx -- "--pidfile=$home/dlog/clientlaunch.$host.pid" "$home/launch.args" \
	|| fail "with $name, the pidfile is not in the directory xymonclient.d names: $(cat "$home/launch.args")"

pass "runclient.sh checks and uses the log and tmp directories xymonclient.cfg names, its xymonclient.d included, and those under XYMONCLIENTHOME by default"
