#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/dropdirectory-symlink.sh
#
# dropdirectory() (lib/files.c) recursively removes a directory tree. It used
# stat() and a bare opendir(), so a symlink planted as the target or as a child
# was followed and whatever it pointed at, outside the tree, was deleted: a
# "histlogs/<host> -> ../../elsewhere" turned a @@drophost into a recursive
# delete of elsewhere. It uses lstat() now and removes a symlink as a link.
#
# Driven through the real xymond_history worker (which calls dropdirectory on
# @@drophost), reading its message from stdin (xymond_worker.c).

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

drive() {  # drive <hostname>
	printf '@@drophost#1|1|10.0.0.1|%s\n@@\n' "$1" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_history" --env="$work/etc/xymonserver.cfg" \
		--histdir="$work/var/hist" --histlogdir="$work/var/histlogs" \
		>/dev/null 2>>"$work/worker.log"
	sleep 0.3  # dropdirectory(background=1) forks; let the child finish
}

setup() {
	rm -rf "$work/var"
	mkdir -p "$work/var/hist" "$work/var/histlogs/realhost/conn"
	printf 'histlog\n' >"$work/var/histlogs/realhost/conn/10000000"
	# A directory outside histlogdir, and a symlink to it inside histlogdir.
	mkdir -p "$work/var/OUTSIDE"
	printf 'PRECIOUS\n' >"$work/var/OUTSIDE/keep"
}

# ---- a symlinked histlog dir must not be followed into an outside tree ------
setup
ln -s ../OUTSIDE "$work/var/histlogs/evil"
: >"$work/worker.log"
drive 'evil'
[ -e "$work/var/OUTSIDE/keep" ] \
	|| fail "drophost on a symlinked histlog dir followed it and deleted an outside tree"
[ ! -e "$work/var/histlogs/evil" ] \
	|| fail "the planted symlink itself was left behind: $(ls -l "$work/var/histlogs/evil")"

# ---- a symlinked *child* inside the tree is unlinked, not followed ----------
setup
ln -s ../../OUTSIDE "$work/var/histlogs/realhost/link"
: >"$work/worker.log"
drive 'realhost'
[ -e "$work/var/OUTSIDE/keep" ] \
	|| fail "a symlinked child under the host tree was followed and its target deleted"
[ ! -e "$work/var/histlogs/realhost" ] \
	|| fail "the real histlog tree was not removed by a legitimate drophost"

# ---- positive control: a real histlog tree is still removed -----------------
setup
: >"$work/worker.log"
drive 'realhost'
[ ! -e "$work/var/histlogs/realhost" ] \
	|| { cat "$work/worker.log" >&2; fail "a legitimate drophost did not remove the host's histlog tree"; }

echo "OK $(basename "$0")"
