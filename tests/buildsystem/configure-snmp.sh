#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/configure-snmp.sh
#
# SNMP support is chosen like RRD, SSL and LDAP: configure.server probes
# Net-SNMP through build/snmp.sh and, when it is found, asks whether to enable
# it, with "n" the default. ENABLESNMP=y answers yes, SNMP=1 is still accepted
# for it, and --snmpinclude/--snmplib name a location instead of the probe.
# What the probe takes from net-snmp-config is only include dirs, defines and
# library flags: the rest is the distribution's own build policy
# (-Werror=declaration-after-statement, -specs=... for hardened links), which
# would break or change this build.
#
# Each case runs ./configure --server in a scratch copy of configure,
# configure.server and build/, with every other dependency probe replaced by
# a stub that reports it found, so configure runs to the end on any lane, and
# with a fake net-snmp-config on PATH. The result is read from the Makefile
# it writes.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/configure.server" ] || skip "configure.server missing"
[ -f "$ROOT/build/snmp.sh" ] || skip "build/snmp.sh missing"

TMP=$(mktempdir)
SRC="$TMP/src"
mkdir -p "$SRC" "$TMP/fakebin" "$TMP/nosnmp"
cp -p "$ROOT/configure" "$ROOT/configure.server" "$SRC/"
cp -rp "$ROOT/build" "$SRC/build"
[ -f "$ROOT/configure.client" ] && cp -p "$ROOT/configure.client" "$SRC/"

# Every probe before SNMP's reports what it found, or that the option is off.
printf 'FPING="/bin/true"\n' >"$SRC/build/fping.sh"
printf 'PCREOK="YES"; PCREINCDIR=""; PCRELIBS="-lpcre2-8"\n' >"$SRC/build/pcre.sh"
printf 'SYSTEMCARES="yes"; CARESINCDIR=""; CARESLIBS="-lcares"\n' >"$SRC/build/c-ares.sh"
printf 'RRDOK="YES"; RRDDEF=""; RRDINCDIR=""; RRDLIBS="-lrrd"\n' >"$SRC/build/rrd.sh"
printf 'SSLOK="NO"\n' >"$SRC/build/ssl.sh"
printf 'LDAPOK="NO"\n' >"$SRC/build/ldap.sh"
printf 'LIBRTDEF=""\n' >"$SRC/build/clock-gettime-librt.sh"

# A Net-SNMP whose flags carry build policy along with what is needed.
cat >"$TMP/fakebin/net-snmp-config" <<'EOF'
#!/bin/sh
case "$1" in
--version) echo 5.9.9 ;;
--cflags)  echo "-I/fake/inc -DFAKE -Werror=declaration-after-statement" ;;
--libs)    echo "-L/fake/lib -lnetsnmp -specs=/fake/hardened-ld" ;;
esac
EOF
chmod +x "$TMP/fakebin/net-snmp-config"

make_is_gnu() {
	local version
	version=$(first_line "$("$1" -version 2>&1 || true)")
	[ "$(awk '{print $1 " " $2}' <<<"$version")" = "GNU Make" ]
}
if [ -z "${MAKE:-}" ] && ! make_is_gnu make; then
	if command -v gmake >/dev/null 2>&1 && make_is_gnu gmake; then
		export MAKE=gmake
	fi
fi

# run_configure BINDIR [VAR=value | --option | VALUE ...] -- configures afresh, with
# BINDIR first on PATH, and leaves the Makefile's SNMP lines in $GOT. ENABLESNMP
# and SNMP start empty, so the caller's environment cannot answer for a case: a
# CI lane that builds with SNMP exports SNMP=1.
run_configure() {
	local bindir=$1
	shift
	local args=() envs=()
	for a in "$@"; do
		case $a in
			-*|[!A-Z]*) args+=("$a") ;;
			*=*)        envs+=("$a") ;;
			*)          args+=("$a") ;;
		esac
	done
	rm -f "$SRC/Makefile"
	(
		cd "$SRC"
		env "PATH=$bindir:$PATH" USEXYMONPING=y XYMONUSER="$(id -un)" \
			XYMONTOPDIR=/usr/lib/xymon XYMONVAR=/var/lib/xymon XYMONHOSTURL=/xymon \
			CGIDIR=/usr/lib/xymon/cgi-bin XYMONCGIURL=/xymon-cgi \
			SECURECGIDIR=/usr/lib/xymon/cgi-secure SECUREXYMONCGIURL=/xymon-seccgi \
			HTTPDGID="$(id -gn)" XYMONLOGDIR=/var/log/xymon XYMONHOSTNAME=localhost \
			XYMONHOSTIP=127.0.0.1 MANROOT=/usr/share/man \
			INSTALLBINDIR=/usr/lib/xymon/server/bin INSTALLETCDIR=/etc/xymon \
			INSTALLWEBDIR=/etc/xymon/web INSTALLEXTDIR=/usr/lib/xymon/server/ext \
			INSTALLTMPDIR=/var/lib/xymon/tmp INSTALLWWWDIR=/var/lib/xymon/www \
			ENABLESNMP= SNMP= \
			${envs[@]+"${envs[@]}"} \
			./configure --server ${args[@]+"${args[@]}"} </dev/null >"$TMP/configure.log" 2>&1
	) || fail "configure --server failed: $(cat "$TMP/configure.log")"
	GOT=$(grep -E '^(DOSNMP|SNMPINCDIR|SNMPLIBS) ' "$SRC/Makefile" || true)
}

# The default: found, but not enabled unless asked.
run_configure "$TMP/fakebin"
assert_contains "DOSNMP = no" "$GOT" "SNMP was enabled without being asked for"
assert_not_contains "SNMPLIBS" "$GOT" "a build without SNMP was given SNMP libraries"

# Enabled: only what this build needs is taken from net-snmp-config.
for how in ENABLESNMP=y SNMP=1; do
	run_configure "$TMP/fakebin" "$how"
	assert_contains "DOSNMP = yes" "$GOT" "$how did not enable SNMP"
	assert_contains "-I/fake/inc -DFAKE" "$GOT" "$how lost net-snmp-config's include dirs or defines"
	assert_contains "-L/fake/lib -lnetsnmp" "$GOT" "$how lost net-snmp-config's library flags"
	assert_not_contains "-Werror" "$GOT" "$how took net-snmp-config's -Werror into the build"
	assert_not_contains "-specs" "$GOT" "$how took net-snmp-config's -specs into the build"
done

# Asked for, but Net-SNMP is not there: configure carries on without it. A
# net-snmp-config that fails stands in for none, and shadows a real one.
printf '#!/bin/sh\nexit 1\n' >"$TMP/nosnmp/net-snmp-config"
chmod +x "$TMP/nosnmp/net-snmp-config"
run_configure "$TMP/nosnmp" ENABLESNMP=y
assert_contains "DOSNMP = no" "$GOT" "SNMP was enabled although no Net-SNMP was found"

# A location given on the command line is used instead of the probe.
run_configure "$TMP/nosnmp" ENABLESNMP=y --snmpinclude /opt/snmp/inc --snmplib /opt/snmp/lib
assert_contains "DOSNMP = yes" "$GOT" "--snmpinclude/--snmplib did not enable SNMP"
assert_contains "SNMPINCDIR = -I/opt/snmp/inc" "$GOT" "--snmpinclude was not used"
assert_contains "SNMPLIBS = -L/opt/snmp/lib -lnetsnmp" "$GOT" "--snmplib was not used"

pass "configure.server enables SNMP only when asked, by ENABLESNMP=y or SNMP=1, takes only include, define and library flags from net-snmp-config, and honours --snmpinclude/--snmplib"
