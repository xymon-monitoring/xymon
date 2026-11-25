#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/xymond-client-valgrind-clean.sh
#
# build/valgrind.sh counts a block that is definitely lost as a failure.
# xymond_client allocated its --config= file name and its collector list in
# main() and never freed them: once main() returned, Memcheck counted both as
# lost, so build/valgrind.sh failed on every run and could not be used to
# check xymond_client for a real memory error. A repeated --config= or
# --collectors= also lost the earlier value, and --dump-config returned
# without freeing either.
#
# This runs xymond_client under build/valgrind.sh and expects nothing to be
# reported: on one ordinary report with the default collector list, with
# --collectors=, and with each option given twice; and for --dump-config.
# Without valgrind it skips.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

root=$(find_root)
work=$(mktempdir)
mkdir -p "$work/home"
printf 'DEFAULT\n' > "$work/analysis.cfg"

# vgrun NAME [ARGUMENT...]: xymond_client under valgrind, one report on stdin.
vgrun() {
	local name=$1 rc=0
	shift
	printf '@@client|1755640000|127.0.0.1|vg.test|linux|linux|\n[date]\nWed Nov 25 07:00:00 UTC 2026\n@@\n' |
		env XYMONHOME="$work/home" XYMONTMP="$work" VALGRIND_LOG="$work/$name.log" \
		"$root/build/valgrind.sh" "$XYMOND_CLIENT" "$@" >/dev/null 2>"$work/$name.err" || rc=$?
	case $rc in
	  0)  ;;
	  77) skip "valgrind is not installed" ;;
	  99) fail "$name: Memcheck reported xymond_client: $(grep -E 'definitely lost:|indirectly lost:|ERROR SUMMARY' "$work/$name.log" | sed 's/^==[0-9]*== *//' | tr '\n' ';')" ;;
	  *)  fail "$name: xymond_client exited $rc under valgrind: $(tail -n 3 "$work/$name.err")" ;;
	esac
}

cfg="--config=$work/analysis.cfg"
vgrun default --local --no-update "$cfg"
vgrun collectors --local --no-update "$cfg" --collectors=a,b
vgrun twice --local --no-update "$cfg" "$cfg" --collectors=a --collectors=b
vgrun dumpconfig "$cfg" --collectors=a --dump-config

pass "xymond_client leaves nothing for Memcheck to report, on a report and on --dump-config"
