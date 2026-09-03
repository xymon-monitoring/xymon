#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/history-writer-symlink.sh
#
# The @@stachg writers opened their files with fopen(), which follows a symlink
# at the final component, and built "<histlogdir>/<host>/..." which follows a
# symlinked <host> intermediate. A symlink planted at a history file, or as a
# host's histlog dir, therefore had the daemon write/append/truncate through it,
# outside the tree. The final opens use O_NOFOLLOW now, and the histlog
# intermediate host dir is refused when it is a symlink.
#
# Driven through the real xymond_history worker (reads its message from stdin).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

ROOT=$(find_root)

work=$(mktempdir)
build_xymond_worker "$work" xymond_history \
	xymond/xymond_history.c xymond/xymond_worker.c

mkdir -p "$work/etc"
cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

stachg() {  # stachg <host> <test>
	printf '@@stachg#1|1|10.0.0.1|origin|%s|%s|0|red|1|red|1000|green|0|0|0|0\nBODY\n@@\n' "$1" "$2" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_history" --env="$work/etc/xymonserver.cfg" \
		--histdir="$work/var/hist" --histlogdir="$work/var/histlogs" \
		>/dev/null 2>>"$work/worker.log"
}

setup() {
	rm -rf "$work/var"
	mkdir -p "$work/var/hist" "$work/var/histlogs"
}

# ---- host-events final symlink: hist/<host> -> ../pwned ---------------------
setup
ln -s ../pwned "$work/var/hist/myhost"
: >"$work/worker.log"
stachg 'myhost' 'conn'
[ ! -e "$work/var/pwned" ] \
	|| fail "host-events followed a symlinked hist/<host> and wrote outside histdir"

# ---- status-events final symlink: hist/<host>.<test> -> ../pwned ------------
setup
ln -s ../pwned "$work/var/hist/myhost.conn"
: >"$work/worker.log"
stachg 'myhost' 'conn'
[ ! -e "$work/var/pwned" ] \
	|| fail "status-events followed a symlinked hist/<host>.<test> and wrote outside histdir"

# ---- histlog intermediate symlink: histlogs/<host> -> ../OUTSIDE ------------
setup
mkdir -p "$work/var/OUTSIDE"
ln -s ../OUTSIDE "$work/var/histlogs/myhost"
: >"$work/worker.log"
stachg 'myhost' 'conn'
[ -z "$(find "$work/var/OUTSIDE" -name conn -o -name '*conn*' 2>/dev/null | head -1)" ] \
	|| fail "histlog writer followed a symlinked host dir and created files in an outside tree"
grep -q 'is a symlink' "$work/worker.log" \
	|| fail "a symlinked histlog host dir was not refused"

# ---- histlog *test-level* intermediate symlink: histlogs/<host>/<test> ------
# The host dir is real, but the test dir under it is a symlink. The final open
# guards only its own component, so the test dir must be refused too.
setup
mkdir -p "$work/var/histlogs/myhost" "$work/var/OUTSIDE"
ln -s ../../OUTSIDE "$work/var/histlogs/myhost/conn"
: >"$work/worker.log"
stachg 'myhost' 'conn'
[ -z "$(find "$work/var/OUTSIDE" -type f 2>/dev/null | head -1)" ] \
	|| fail "histlog writer followed a symlinked test dir and wrote into an outside tree"
grep -q 'is a symlink' "$work/worker.log" \
	|| fail "a symlinked histlog test dir was not refused"

# ---- positive control: a real host still records to real files --------------
setup
: >"$work/worker.log"
stachg 'realhost' 'conn'
[ -e "$work/var/hist/realhost.conn" ] && [ -e "$work/var/hist/realhost" ] \
	|| { cat "$work/worker.log" >&2; fail "a legitimate status change was not recorded to real files"; }
[ -n "$(find "$work/var/histlogs/realhost/conn" -type f 2>/dev/null)" ] \
	|| fail "a legitimate status change wrote no histlog file"

echo "OK $(basename "$0")"
