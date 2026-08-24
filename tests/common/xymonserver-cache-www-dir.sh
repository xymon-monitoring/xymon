#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/common/xymonserver-cache-www-dir.sh
#
# The availability reports and the snapshots are regenerable output, so the
# shipped xymonserver.cfg puts both under one setting, XYMONCACHEWWWDIR, which
# defaults to XYMONWWWDIR. Left alone, XYMONREPDIR and XYMONSNAPDIR resolve
# where they always did, $XYMONWWWDIR/rep and /snap; set it, and both follow.
#
# The shipped xymonserver.cfg.DIST is loaded the way every program loads its
# configuration, through xymoncmd --env, with its build-time placeholders
# filled in. The shipped file is the surface here, so it is read as shipped.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
dist="$ROOT/xymond/etcfiles/xymonserver.cfg.DIST"
[ -f "$dist" ] || skip "xymonserver.cfg.DIST is not in this tree"

# Server builds produce common/xymoncmd; client-only builds ship the same
# tool as client/xymoncmd.
default="common/xymoncmd"
if [ -z "${XYMONCMD:-}" ] && [ ! -x "$ROOT/$default" ] \
		&& [ -x "$ROOT/client/xymoncmd" ]; then
	default="client/xymoncmd"
fi
require_bin XYMONCMD "$default"

work=$(mktempdir)
sed -e "s!@XYMONHOME@!$work/home!g" -e "s!@[A-Z]*@!x!g" "$dist" >"$work/xymonserver.cfg"

# resolved CFG VAR -- the value VAR has once CFG is loaded. Read from the
# child's environment: xymoncmd expands $VARs in its own arguments before it
# loads the file, so a "$VAR" in the command line would see the built-in
# defaults instead.
resolved() {
	sed -n "s/^$2=//p" <<<"$("$XYMONCMD" --env="$1" env 2>/dev/null)"
}

www=$(resolved "$work/xymonserver.cfg" XYMONWWWDIR)
[ -n "$www" ] || fail "XYMONWWWDIR did not resolve from the shipped xymonserver.cfg"

# ---- left alone: where they always were -------------------------------------
assert_equal "$www/rep" "$(resolved "$work/xymonserver.cfg" XYMONREPDIR)" \
	"XYMONREPDIR moved although XYMONCACHEWWWDIR was not set"
assert_equal "$www/snap" "$(resolved "$work/xymonserver.cfg" XYMONSNAPDIR)" \
	"XYMONSNAPDIR moved although XYMONCACHEWWWDIR was not set"

# ---- set once: both follow ---------------------------------------------------
sed 's!^XYMONCACHEWWWDIR=.*!XYMONCACHEWWWDIR="/var/cache/xymon/www"!' \
	"$work/xymonserver.cfg" >"$work/moved.cfg"
grep -q '^XYMONCACHEWWWDIR="/var/cache/xymon/www"' "$work/moved.cfg" \
	|| fail "the shipped xymonserver.cfg has no XYMONCACHEWWWDIR line to set"
assert_equal "/var/cache/xymon/www/rep" "$(resolved "$work/moved.cfg" XYMONREPDIR)" \
	"XYMONREPDIR did not follow XYMONCACHEWWWDIR"
assert_equal "/var/cache/xymon/www/snap" "$(resolved "$work/moved.cfg" XYMONSNAPDIR)" \
	"XYMONSNAPDIR did not follow XYMONCACHEWWWDIR"

pass "the shipped xymonserver.cfg keeps rep and snap under XYMONWWWDIR by default, and moves both when XYMONCACHEWWWDIR is set"
