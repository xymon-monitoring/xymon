#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/ssl-expiry-defaults.sh
#
# A certificate turns the sslcert column yellow 14 days before it expires
# and red 7 days before, unless --sslwarn/--sslalarm or a host's ssldays tag
# says otherwise (#635). The defaults were 30 and 10, which shows yellow for
# days of every cycle of a certificate renewed automatically about three
# weeks before expiry, as 64-day Let's Encrypt certificates are.
#
# Checked through xymonnet --dump=before, which prints each host's
# thresholds as "SSL alerts : WARN:ALARM" before any test runs: a host
# without ssldays gets 14:7; a host with ssldays=30:10 keeps 30:10; and
# --sslwarn=20 --sslalarm=5 still set the defaults. The hosts' https tests
# point at a port where nothing listens, so the run after the dump ends at
# once.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp"
cp "$(find_root)/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
cat >"$work/home/etc/hosts.cfg" <<'EOF2'
127.0.0.1 plainhost # testip https://127.0.0.1:1/
127.0.0.1 tunedhost # testip https://127.0.0.1:1/ ssldays=30:10
EOF2

# alerts HOST [OPTIONS...] -- the "SSL alerts" value xymonnet dumps for HOST
alerts() {
	local host=$1
	shift
	XYMONHOME="$work/home" XYMONTMP="$work/tmp" HOSTSCFG="$work/home/etc/hosts.cfg" \
		"$XYMONNET" --noping --no-update --timeout=2 --dump=before "$@" >"$work/dump" 2>&1 || true
	awk -v h="$host" '/Hostname: /{cur=$NF} /SSL alerts/ && cur==h {print $NF; exit}' "$work/dump"
}

assert_equal "14:7" "$(alerts plainhost)" \
	"a host without ssldays did not get the default thresholds of 14 and 7 days"
assert_equal "30:10" "$(alerts tunedhost)" \
	"a host's ssldays=30:10 no longer overrides the defaults"
assert_equal "20:5" "$(alerts plainhost --sslwarn=20 --sslalarm=5)" \
	"--sslwarn and --sslalarm no longer set the defaults"

pass "a certificate warns 14 days and alarms 7 days before expiry by default; ssldays and --sslwarn/--sslalarm still override"
