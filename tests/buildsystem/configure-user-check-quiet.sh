#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Bruno Manzoni
#
# tests/buildsystem/configure-user-check-quiet.sh
#
# configure looks the Xymon user up with getent, then ypmatch, then
# /etc/passwd, and keeps each one's errors out of its output. ypmatch's were
# not: on a system without NIS tools, as RHEL 9 by default, a user that
# getent did not find printed "ypmatch: command not found" before the
# FAILURE line (reported on the list, testing 4.3.31).
#
# Checked for configure.server and configure.client: with XYMONUSER set to a
# user that does not exist, and a ypmatch first on PATH that writes an error
# and fails, configure stops with its FAILURE line and that error does not
# appear. On macOS configure asks the directory service instead, so there the
# check passes without reaching ypmatch. configure asks for the user only once
# its prerequisites are found (RRDtool and PCRE for the server); where they
# are not, that script is reported as not checked.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/configure.server" ] || skip "configure.server missing"
[ -d "$ROOT/build" ]            || skip "build/ missing"

work=$(mktempdir)
src="$work/src"
mkdir -p "$src" "$work/bin"
cp -p "$ROOT"/configure* "$src/"
cp -rp "$ROOT/build" "$src/build"
cp -rp "$ROOT/lib" "$src/lib"	# the RRDtool probe includes lib/rrd_api_compat.h

printf '#!/bin/sh\necho "ypmatch: fake NIS failure" >&2\nexit 1\n' > "$work/bin/ypmatch"
chmod +x "$work/bin/ypmatch"

nosuch="xymon-test-nosuchuser-$$"
getent passwd "$nosuch" >/dev/null 2>&1 && skip "user $nosuch exists here"

missing=""
for kind in server client; do
	rc=0
	# configure checks the user only when XYMONTOPDIR is not given, and CI
	# gives configure its answers through the environment.
	(cd "$src" && unset XYMONTOPDIR && env PATH="$work/bin:$PATH" XYMONUSER="$nosuch" USEXYMONPING=y \
		sh "./configure.$kind" </dev/null) >"$work/$kind.out" 2>&1 || rc=$?
	out=$(cat "$work/$kind.out")
	case $out in
		*"What userid will be running Xymon"*) ;;
		*) missing="${missing:+$missing; }configure.$kind (its prerequisites are not met here: $(tail -1 "$work/$kind.out"))"; continue ;;
	esac
	[ "$rc" -ne 0 ] || fail "configure.$kind accepted a user that does not exist"
	assert_contains "FAILURE: The user $nosuch does not exist" "$out" \
		"configure.$kind did not stop at the user check: $(tail -5 "$work/$kind.out")"
	assert_not_contains "fake NIS failure" "$out" \
		"configure.$kind printed ypmatch's error while looking the user up"
done

[ -z "$missing" ] || pass_partial "configure reports a missing user without ypmatch's errors" "not checked: $missing"
pass "configure.server and configure.client report a missing user without ypmatch's errors"
