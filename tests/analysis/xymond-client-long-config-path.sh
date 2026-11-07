#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/xymond-client-long-config-path.sh
#
# xymond_client reports a configuration file name too long for its buffer,
# and does not crash on it (#643). It built XYMONHOME/etc/analysis.cfg with
# sprintf(), and copied a --config name with strcpy(), into a PATH_MAX
# buffer: a long enough name overflowed it, and glibc aborted the program.
#
# Checked with --dump-config, which loads the configuration and exits: a
# 5000-character XYMONHOME, and then a 5000-character --config name, each
# give exit 0 and a "too long" error.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
long=$(printf '%5000s' '' | tr ' ' A)

rc=0
(cd "$work" && env XYMONHOME="$long" "$XYMOND_CLIENT" --dump-config) >/dev/null 2>"$work/home.err" || rc=$?
assert_equal "0" "$rc" "xymond_client did not survive a 5000-character XYMONHOME: $(tail -c 300 "$work/home.err")"
assert_contains "too long" "$(cat "$work/home.err")" "xymond_client did not report the analysis.cfg name as too long"

rc=0
(cd "$work" && "$XYMOND_CLIENT" --config="/$long" --dump-config) >/dev/null 2>"$work/config.err" || rc=$?
assert_equal "0" "$rc" "xymond_client did not survive a 5000-character --config name: $(tail -c 300 "$work/config.err")"
assert_contains "too long" "$(cat "$work/config.err")" "xymond_client did not report the --config name as too long"

pass "xymond_client reports an analysis.cfg or --config name too long for its buffer, and does not crash"
