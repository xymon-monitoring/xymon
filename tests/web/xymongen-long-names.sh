#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xymongen-long-names.sh
#
# xymongen reports a file name too long for its buffer, and does not crash
# on it (#643). It built two names with sprintf() into PATH_MAX buffers: an
# extension's path, XYMONHOME/ext/mkbb/NAME, for each name in
# XYMONNONGREENEXT; and the Netscape sidebar file given with --nstab, and
# its temporary. A long enough name overflowed them, and glibc aborted the
# program.
#
# Checked against a running xymond with one red status, so xymongen builds
# the non-green page where both are used: a 5000-character extension name,
# and then a 5000-character --nstab name, each give exit 0 and a "too long"
# error, and the pages are still written.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONGEN xymongen/xymongen
require_bin XYMONCMD common/xymoncmd
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www" "$work/home/ext/mkbb" "$work/data"
printf 'page test Test\n127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

xymond_launch() {
	local port=$1; shift
	sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
	    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	    -e 's|^XYMONVAR=.*|XYMONVAR="'"$work"'/data"|' \
	    -e 's|^XYMONWWWDIR=.*|XYMONWWWDIR="'"$work"'/home/www"|' \
	    -e 's|^HOSTSCFG=.*|HOSTSCFG="'"$work"'/hosts.cfg"|' \
	    -e 's|^XYMSRV=.*|XYMSRV="127.0.0.1"|' \
	    -e 's|^XYMSERVERS=.*|XYMSERVERS=""|' \
	    -e 's|^XYMONDPORT=.*|XYMONDPORT="'"$port"'"|' \
		"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" > "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}
start_xymond
register_cleanup "stop_process $XYMOND_PID"
"$XYMONCLIENT" "127.0.0.1:$PORT" "status testhost.conn red down" \
	|| fail "could not send a status to xymond: $(cat "$work/xymond.log")"

long=$(printf '%5000s' '' | tr ' ' A)

# gen LABEL [ENV=VALUE...] -- [XYMONGEN ARGS...]: run xymongen, set rc and err
gen() {
	local label=$1; shift
	local envs=()
	while [ "$1" != "--" ]; do envs+=("$1"); shift; done
	shift
	rc=0
	(cd "$work" && "$XYMONCMD" --env="$work/xymonserver.cfg" env "${envs[@]}" \
		"$XYMONGEN" "$@" "$work/home/www") >/dev/null 2>"$work/$label.err" || rc=$?
	err=$(cat "$work/$label.err")
}

gen ext "XYMONNONGREENEXT=$long" --
assert_equal "0" "$rc" "xymongen did not survive a 5000-character extension name: $(tail -c 300 "$work/ext.err")"
assert_contains "Cannot run an extension: its name under XYMONHOME is too long" "$err" \
	"xymongen did not report the extension name as too long"
assert_file_exists "$work/home/www/nongreen.html" "xymongen wrote no non-green page"

gen nstab "XYMONNONGREENEXT=" -- "--nstab=/$long"
assert_equal "0" "$rc" "xymongen did not survive a 5000-character --nstab name: $(tail -c 300 "$work/nstab.err")"
assert_contains "Cannot create Netscape sidebar outputfile: the name is too long" "$err" \
	"xymongen did not report the --nstab name as too long"

pass "xymongen reports an extension or --nstab name too long for its buffer, and does not crash"
