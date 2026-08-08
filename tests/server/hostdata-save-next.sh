#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMOND_HOSTDATA xymond/xymond_hostdata
require_bin XYMON common/xymon

work=$(mktempdir)
port=$((20000 + ($$ % 20000)))
mkdir -p "$work"/{home/etc,home/tmp,var,logs,hostdata}
printf '127.0.0.1 testhost # noconn\n' >"$work/home/etc/hosts.cfg"
cat >"$work/xymonserver.cfg" <<EOF
XYMONHOME="$work/home"
XYMONVAR="$work/var"
XYMONSERVERLOGS="$work/logs"
XYMONTMP="$work/home/tmp"
CLIENTLOGS="$work/hostdata"
HOSTSCFG="$work/home/etc/hosts.cfg"
XYMONSERVERHOSTNAME="testhost"
XYMONSERVERIP="127.0.0.1"
MACHINEDOTS="testhost"
XYMONDPORT="$port"
EOF

stop_daemons() {
	"$XYMON" "127.0.0.1:$port" shutdown >/dev/null 2>&1 || true
	for pidfile in "$work/channel.pid" "$work/xymond.pid"; do
		if [ -f "$pidfile" ]; then
			kill "$(cat "$pidfile")" 2>/dev/null || true
		fi
	done
}
register_cleanup stop_daemons

"$XYMOND" --env="$work/xymonserver.cfg" --hosts="$work/home/etc/hosts.cfg" \
	--listen="127.0.0.1:$port" --pidfile="$work/xymond.pid" --daemon

response=''
for attempt in {1..1000}; do
	response=$("$XYMON" "127.0.0.1:$port" 'hostdatasave testhost nope' 2>/dev/null || true)
	[ -n "$response" ] && break
done
assert_contains 'ERROR: hostdatasave requires HOSTNAME' "$response" \
	"an invalid lifetime must be rejected immediately"
response=$("$XYMON" "127.0.0.1:$port" hostdatasave)
assert_contains 'ERROR: hostdatasave requires HOSTNAME' "$response" \
	"a missing hostname must be rejected immediately"

response=$("$XYMON" "127.0.0.1:$port" 'hostdatasave unknown-host 5')
assert_equal 'ERROR: unknown host unknown-host' "$response"
response=$("$XYMON" "127.0.0.1:$port" 'hostdatasave testhost 5')
assert_equal 'ERROR: no hostdata worker is listening' "$response"

"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=clichg \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/hostdata.log" \
	"$XYMOND_HOSTDATA" --env="$work/xymonserver.cfg" \
	--logdir="$work/hostdata" --minimum-free=0 --recent-count=0

response=''
for attempt in {1..1000}; do
	response=$("$XYMON" "127.0.0.1:$port" 'hostdatasave testhost 5' 2>/dev/null || true)
	[[ $response == OK:* ]] && break
done
assert_equal 'OK: next client report for testhost armed for 5 minutes' "$response"

"$XYMON" "127.0.0.1:$port" $'client testhost.linux\n[test]\nforced payload'
saved=''
for attempt in {1..1000}; do
	saved=$(find "$work/hostdata/testhost" -type f -print -quit 2>/dev/null || true)
	[ -n "$saved" ] && break
done
assert_file_exists "$saved" "the armed client report must reach the hostdata worker"
assert_contains 'forced payload' "$(cat "$saved")" \
	"the forced snapshot payload changed"

"$XYMON" "127.0.0.1:$port" $'client testhost.linux\n[test]\nsecond payload'
assert_equal '1' "$(find "$work/hostdata/testhost" -type f | wc -l)" \
	"the request must be consumed after one client report"
assert_not_contains 'second payload' "$(cat "$saved")" \
	"a consumed request was applied to a later client report"

pass "hostdatasave arms one forced, quota-bypassing client snapshot"
