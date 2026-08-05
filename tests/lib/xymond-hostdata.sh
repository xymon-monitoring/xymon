# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash
#
# tests/lib/xymond-hostdata.sh -- shared build/run recipe for the
# xymond_hostdata tests. Source after assert.sh, then:
#
#     setup_xymond_hostdata "$work"
#     ... | run_xymond_hostdata "$work" <worker options>
#
# run_xymond_hostdata feeds its stdin (a CLICHG channel stream) to a freshly
# built worker with the tree's stock environment and an empty
# <work>/var/hostdata; saved files appear under <work>/var/hostdata/<host>/.

[ -n "${__XYMON_TESTS_HOSTDATA_SOURCED:-}" ] && return 0
__XYMON_TESTS_HOSTDATA_SOURCED=1

# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "${BASH_SOURCE[0]}")/build-worker.sh"

setup_xymond_hostdata() {
	local work=$1
	build_xymond_worker "$work" xymond_hostdata \
		xymond/xymond_hostdata.c xymond/xymond_worker.c
	mkdir -p "$work/etc"
	cp "$(find_root)/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"
}

# Run the worker against <work>/var without wiping it first, so the caller
# can pre-seed the hostdata tree (e.g. a blocker that makes a save fail).
# The caller must have created <work>/var/hostdata.
run_xymond_hostdata_keepvar() {
	local work=$1
	shift
	XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_hostdata" --env="$work/etc/xymonserver.cfg" \
		--logdir="$work/var/hostdata" --minimum-free=0 "$@"
}

run_xymond_hostdata() {
	local work=$1
	shift
	rm -rf "${work:?}/var"; mkdir -p "$work/var/hostdata"
	run_xymond_hostdata_keepvar "$work" "$@"
}
