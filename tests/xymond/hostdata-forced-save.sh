#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_HOSTDATA xymond/xymond_hostdata

ROOT=$(find_root)
work=$(mktempdir)
mkdir -p "$work/etc" "$work/var/hostdata"
cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

set +e
{
	printf '@@clichg#1|1|10.0.0.99|realhost|normal\nnormal payload\n@@\n'
	printf '@@clichg#2|1|10.0.0.99|realhost|forced|forced\nforced payload\n@@\n'
} | XYMONHOME="$work" XYMONVAR="$work/var" \
	"$XYMOND_HOSTDATA" --env="$work/etc/xymonserver.cfg" \
	--logdir="$work/var/hostdata" --minimum-free=0 --recent-count=0 \
	>"$work/worker.out" 2>"$work/worker.err"
rc=$?
set -e
[ "$rc" -le 1 ] || { cat "$work/worker.err" >&2; fail "xymond_hostdata exited $rc"; }

[ ! -e "$work/var/hostdata/realhost/normal" ] \
	|| fail "an ordinary save bypassed --recent-count=0"
assert_file_exists "$work/var/hostdata/realhost/forced" \
	"a forced save must bypass --recent-count=0"
assert_equal "forced payload" "$(cat "$work/var/hostdata/realhost/forced")" \
	"the forced snapshot payload changed"

pass "forced hostdata saves bypass the recent-save quota"