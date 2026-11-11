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
# Checked on the shared status board (tests/lib/xymongen-board.sh), whose red
# status makes xymongen build the non-green page where both names are used: a
# 5000-character extension name, and then a 5000-character --nstab name, each
# give exit 0 and a "too long" error, and the pages are still written.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymongen-board.sh
. "$(dirname "$0")/../lib/xymongen-board.sh"

require_bin XYMONGEN xymongen/xymongen

board_setup
mkdir -p "$work/home/ext/mkbb"

long=$(printf '%5000s' '' | tr ' ' A)

board_render ext "XYMONNONGREENEXT=$long" --
assert_equal "0" "$RC" "xymongen did not survive a 5000-character extension name: $(tail -c 300 "$work/ext.err")"
assert_contains "Cannot run an extension: its name under XYMONHOME is too long" "$ERR" \
	"xymongen did not report the extension name as too long"
assert_file_exists "$work/www/nongreen.html" "xymongen wrote no non-green page"

board_render nstab -- "--nstab=/$long"
assert_equal "0" "$RC" "xymongen did not survive a 5000-character --nstab name: $(tail -c 300 "$work/nstab.err")"
assert_contains "Cannot create Netscape sidebar outputfile: the name is too long" "$ERR" \
	"xymongen did not report the --nstab name as too long"

pass "xymongen reports an extension or --nstab name too long for its buffer, and does not crash"
