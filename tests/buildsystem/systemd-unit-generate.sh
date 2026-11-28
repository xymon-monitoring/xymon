#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/systemd-unit-generate.sh
#
# "make install" generates the systemd unit from tools/xymonlaunch.service.DIST
# with this build's own paths, so the installed unit works wherever the tree
# was put. It is generated only when INSTALLSYSTEMDDIR names where systemd
# reads units; unset, the .DIST stays a sample and nothing is installed -- what
# a source install on a host without systemd wants.
#
# @XYMONHOME@, @XYMONUSER@ and @INSTALLENVDIR@ are the placeholders the rule
# fills. A placeholder the .DIST carries but the rule does not substitute would
# ship a unit with a literal @VAR@ that systemd cannot read, so the test pins
# every placeholder on both sides; then it generates into a scratch INSTALLROOT
# and confirms the values landed, nothing literal is left, and an unset
# INSTALLSYSTEMDDIR installs nothing.
#
# Driven through "make -f build/Makefile.rules", the file that carries the
# rule, so the check needs no configured or built tree: the recipe is
# build/subst.sh and a mkdir with the paths passed on the command line.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
dist="$ROOT/tools/xymonlaunch.service.DIST"
rules="$ROOT/build/Makefile.rules"
for f in "$dist" "$rules"; do
	[ -f "$f" ] || fail "missing shipped file: $f"
done

# Every @VAR@ the .DIST carries is substituted by the install-systemd rule. The
# rule sets VAR='$(VAR)' for build/subst.sh; pin it as a fixed string so a
# renamed or dropped name fails here rather than at install time.
placeholders=$(grep -oE '@[A-Z_]+@' "$dist" | sort -u)
[ -n "$placeholders" ] || fail "tools/xymonlaunch.service.DIST carries no @PLACEHOLDER@ to substitute"
for ph in $placeholders; do
	name=${ph#@}; name=${name%@}
	grep -qF "$name='\$($name)'" "$rules" \
		|| fail "build/Makefile.rules install-systemd does not substitute $ph with \$($name): the installed unit would keep it literal"
done

require_gnu_make
# The converted install recipes call build/install-safe; without a build it is
# absent, as in the no-build suite lane, so skip rather than fail there.
require_bin INSTALLSAFE_BIN build/install-safe

# ---- INSTALLSYSTEMDDIR set: the unit is generated with the given paths -------
work=$(mktempdir); register_cleanup "rm -rf '$work'"
"$XYMON_MAKE" -C "$ROOT" -f build/Makefile.rules install-systemd \
	INSTALLROOT="$work" INSTALLSYSTEMDDIR=/lib/systemd/system \
	XYMONHOME=/opt/xymon XYMONUSER=xymontest INSTALLENVDIR=/etc/sysconfig \
	>"$work/gen.log" 2>&1 \
	|| fail "install-systemd failed with INSTALLSYSTEMDDIR set: $(cat "$work/gen.log")"

unit="$work/lib/systemd/system/xymonlaunch.service"
[ -f "$unit" ] || fail "install-systemd did not generate $unit"

left=$({ grep -oE '@[A-Z_]+@' "$unit" || true; } | sort -u | tr '\n' ' ')
[ -z "$left" ] || fail "the generated unit still carries a literal placeholder: $left"

grep -q '^User=xymontest$' "$unit" \
	|| fail "XYMONUSER was not substituted into the unit's User= line"
grep -q '/opt/xymon/bin/xymonlaunch' "$unit" \
	|| fail "XYMONHOME was not substituted into the unit's ExecStart"
grep -q '/etc/sysconfig/xymonlaunch' "$unit" \
	|| fail "INSTALLENVDIR was not substituted into the unit's EnvironmentFile"
grep -q '^RuntimeDirectory=xymon$' "$unit" \
	|| fail "the unit lost RuntimeDirectory=xymon, which provisions the runtime dir for a standalone install"

grep -q '^SendSIGKILL=no$' "$unit" \
	|| fail "the unit lost SendSIGKILL=no, which keeps a task still writing its checkpoint from being SIGKILLed when a stop times out"

# ---- INSTALLSYSTEMDDIR unset: nothing is installed --------------------------
"$XYMON_MAKE" -C "$ROOT" -f build/Makefile.rules install-systemd \
	INSTALLROOT="$work/noop" INSTALLSYSTEMDDIR= \
	>"$work/noop.log" 2>&1 \
	|| fail "install-systemd failed with INSTALLSYSTEMDDIR empty: $(cat "$work/noop.log")"
[ ! -e "$work/noop/lib/systemd/system/xymonlaunch.service" ] \
	|| fail "install-systemd generated a unit when INSTALLSYSTEMDDIR was unset, which a source install does not want"

pass "install generates the systemd unit from this build's paths when INSTALLSYSTEMDDIR is set, substitutes every placeholder, and installs nothing when it is unset"
