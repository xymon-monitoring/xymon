#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/xymonclient-cfg-dirs.sh
#
# The client's configuration names where it writes its own files: XYMONTMP for
# temporary files and XYMONCLIENTLOGS for its logs. A build that relocates those
# directories -- INSTALLCLIENTTMPDIR or INSTALLCLIENTLOGDIR set, as the install
# directories already can be -- names the chosen directory in xymonclient.cfg,
# so the client writes there directly and does not depend on the compatibility
# symlink left at the old place. A default build keeps the relocatable
# "$XYMONHOME/tmp", resolved when the client starts, so the tarball still moves.
#
# Checked on the shipped files and by evaluating the makefile that resolves the
# value, not by installing: what can be checked cheaply is that the placeholder
# is there, that the rule substitutes it, that the build hands the directory
# down, and that the default stays relocatable while a set value is named.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
dist="$ROOT/client/xymonclient.cfg.DIST"
mk="$ROOT/client/Makefile"
rules="$ROOT/build/Makefile.rules"
for f in "$dist" "$mk" "$rules"; do
	[ -f "$f" ] || fail "missing shipped file: $f"
done

# The configuration takes each value from its build-time placeholder.
for v in XYMONTMP XYMONCLIENTLOGS; do
	grep -qE "^$v=\"@$v@\"" "$dist" \
		|| fail "client/xymonclient.cfg.DIST does not take $v from the @$v@ placeholder, so a build cannot name the directory without patching the file"
done

# The rule that generates xymonclient.cfg substitutes both placeholders, or the
# installed file keeps a literal @VAR@ and hands it to the client as a path.
crule=$(grep -F 'xymonclient.cfg.DIST' "$mk")
[ -n "$crule" ] || fail "cannot find the line that generates xymonclient.cfg in client/Makefile"
for v in XYMONTMP XYMONCLIENTLOGS; do
	case $crule in
		*"@$v@"*) ;;
		*) fail "xymonclient.cfg.DIST uses @$v@ but client/Makefile does not substitute it: the installed file would keep it literal" ;;
	esac
done
# ... and with the resolved value, not the raw knob: substituting @XYMONTMP@
# with $(XYMONTMP) rather than $(XYMONTMPSUB) would write XYMONTMP="" into a
# default build, which has no runtime default to fall back on.
case $crule in
	*'@XYMONTMP@!$(XYMONTMPSUB)'*) ;;
	*) fail "client/Makefile does not substitute @XYMONTMP@ with its resolved value $(XYMONTMPSUB)" ;;
esac
case $crule in
	*'@XYMONCLIENTLOGS@!$(XYMONCLIENTLOGSSUB)'*) ;;
	*) fail "client/Makefile does not substitute @XYMONCLIENTLOGS@ with its resolved value $(XYMONCLIENTLOGSSUB)" ;;
esac

# The build passes the install-directory knobs to the client sub-make, or a
# configured value never reaches the generated configuration.
for v in INSTALLCLIENTTMPDIR INSTALLCLIENTLOGDIR; do
	grep -q "$v=\"\$($v)\"" "$rules" \
		|| fail "build/Makefile.rules does not pass $v to the client build, so a configured value never reaches xymonclient.cfg"
done

# A default build keeps the relocatable value; a relocated one names the
# directory. Evaluated through client/Makefile, the file that resolves it.
require_gnu_make
work=$(mktempdir); register_cleanup "rm -rf '$work'"
probe="$work/probe.mk"
{
	echo "XYMONHOME = /opt/xymon/client"
	echo "include $ROOT/client/Makefile"
	cat <<'MK'
probe:
	@printf '%s\n' '$(XYMONTMPSUB)'
	@printf '%s\n' '$(XYMONCLIENTLOGSSUB)'
MK
} >"$probe"

run_probe() { "$XYMON_MAKE" -s -f "$probe" "$@" probe 2>/dev/null; }

out=$(run_probe) || fail "could not evaluate client/Makefile for the default build"
[ "$(printf '%s\n' "$out" | sed -n 1p)" = '$XYMONHOME/tmp' ] \
	|| fail "a default build does not keep XYMONTMP relocatable: got [$(printf '%s\n' "$out" | sed -n 1p)]"
[ "$(printf '%s\n' "$out" | sed -n 2p)" = '$XYMONHOME/logs' ] \
	|| fail "a default build does not keep XYMONCLIENTLOGS relocatable: got [$(printf '%s\n' "$out" | sed -n 2p)]"

out=$(run_probe INSTALLCLIENTTMPDIR=/var/tmp/xymon INSTALLCLIENTLOGDIR=/var/log/xymon-client) \
	|| fail "could not evaluate client/Makefile for a relocated build"
[ "$(printf '%s\n' "$out" | sed -n 1p)" = '/var/tmp/xymon' ] \
	|| fail "a set INSTALLCLIENTTMPDIR does not name XYMONTMP in the configuration: got [$(printf '%s\n' "$out" | sed -n 1p)]"
[ "$(printf '%s\n' "$out" | sed -n 2p)" = '/var/log/xymon-client' ] \
	|| fail "a set INSTALLCLIENTLOGDIR does not name XYMONCLIENTLOGS in the configuration: got [$(printf '%s\n' "$out" | sed -n 2p)]"

pass "xymonclient.cfg names XYMONTMP and XYMONCLIENTLOGS from the build's install directories, and keeps them relocatable by default"
