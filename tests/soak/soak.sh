#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/soak/soak.sh - long-running full-stack soak of the self-describing
# pipeline. NOT part of the test suite (deliberately not executable): run it
# by hand with
#
#     bash tests/soak/soak.sh [--cycles N] [--interval SECONDS] [--home DIR]
#
# It launches a real xymond + client-channel worker + status-channel worker
# (update cache ENABLED), then drives synthetic hosts through full client
# messages and marker statuses each cycle: values vary, one mount flaps in
# and out of existence, one instance stays at a constant, one changes
# value mid-run, units and thresholds are declared. The
# RRD worker is restarted every 20 cycles and xymond HUPed every 50.
#
# Each cycle a checker asserts: worker processes alive, logs free of
# overflow/assertion/segfault markers, the fileset index parses, every
# index entry has its file and every file its entry, and worker
# RSS is sampled for growth. Anomalies are appended to $HOME_DIR/ANOMALIES
# and counted; the run exits nonzero if any occurred.

set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)

CYCLES=120
INTERVAL=5
SOAKHOME=""
while [ $# -gt 0 ]; do
	case "$1" in
	  --cycles)   CYCLES="$2"; shift 2 ;;
	  --interval) INTERVAL="$2"; shift 2 ;;
	  --home)     SOAKHOME="$2"; shift 2 ;;
	  *) echo "unknown option $1" >&2; exit 2 ;;
	esac
done
[ -n "$SOAKHOME" ] || SOAKHOME=$(mktemp -d "${TMPDIR:-/tmp}/xymon-soak.XXXXXX")
LIVE="$SOAKHOME"
mkdir -p "$LIVE"/{etc,tmp,log,data/rrd,data/hist,data/histlogs,data/hostdata,data/acks,data/data,data/disabled,data/logs}
ANOM="$LIVE/ANOMALIES"
: >"$ANOM"

note()    { echo "$(date '+%H:%M:%S') $*" >>"$LIVE/soak.log"; }
anomaly() { echo "$(date '+%H:%M:%S') $*" >>"$ANOM"; note "ANOMALY: $*"; }

cp xymond/etcfiles/xymonserver.cfg "$LIVE/etc/"
cp xymond/etcfiles/graphs.cfg "$LIVE/etc/graphs.cfg" 2>/dev/null || cp xymond/etcfiles/graphs.cfg.DIST "$LIVE/etc/graphs.cfg"
cp xymond/etcfiles/rrddefinitions.cfg "$LIVE/etc/" 2>/dev/null || true
: >"$LIVE/etc/analysis.cfg"
sed "s|^XYMONHOME=.*|XYMONHOME=\"$LIVE\"|; s|^XYMONVAR=.*|XYMONVAR=\"$LIVE/data\"|; s|^FQDN=.*|FQDN=\"FALSE\"|; s|^XYMONLOGDIR=.*|XYMONLOGDIR=\"$LIVE/log\"|" \
	"$LIVE/etc/xymonserver.cfg" >"$LIVE/etc/xymonserver.cfg.tmp" \
	&& mv "$LIVE/etc/xymonserver.cfg.tmp" "$LIVE/etc/xymonserver.cfg"

HOSTS="soak1 soak2 soak3"
{
	for h in $HOSTS; do printf '0.0.0.0 %s # linux\n' "$h"; done
} >"$LIVE/etc/hosts.cfg"

start_rrd() {
	XYMONHOME="$LIVE" ./xymond/xymond_channel --env="$LIVE/etc/xymonserver.cfg" --channel=status \
		--daemon --pidfile="$LIVE/ch-rrd.pid" --log="$LIVE/log/ch-rrd.log" \
		./xymond/xymond_rrd --env="$LIVE/etc/xymonserver.cfg" --rrddir="$LIVE/data/rrd"
}

XYMONHOME="$LIVE" ./xymond/xymond --env="$LIVE/etc/xymonserver.cfg" --hosts="$LIVE/etc/hosts.cfg" \
	--pidfile="$LIVE/xymond.pid" --log="$LIVE/log/xymond.log" --daemon
sleep 1
XYMONHOME="$LIVE" ./xymond/xymond_channel --env="$LIVE/etc/xymonserver.cfg" --channel=client \
	--daemon --pidfile="$LIVE/ch-client.pid" --log="$LIVE/log/ch-client.log" \
	./xymond/xymond_client --env="$LIVE/etc/xymonserver.cfg"
start_rrd
sleep 1

cleanup() {
	for p in ch-rrd ch-client xymond; do
		[ -f "$LIVE/$p.pid" ] && kill "$(cat "$LIVE/$p.pid")" 2>/dev/null
	done
}
trap cleanup EXIT HUP INT TERM

send_host() {  # send_host <host> <cycle>
	local h="$1" c="$2"
	local pct=$(( 30 + (c * 7 + ${#h} * 13) % 60 ))
	local used=$(( pct * 10000 ))
	{
		printf 'client %s.linux linux\n' "$h"
		printf '[df]\n'
		printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
		printf '/dev/sda1 1000000 %s %s %s%% /\n' "$used" $((1000000-used)) "$pct"
		# a flapping mount: present only on even cycles
		if [ $((c % 2)) -eq 0 ]; then
			printf '/dev/sdf1 500000 250000 250000 50%% /flap\n'
		fi
		printf '[inode]\n'
		printf 'Filesystem Inodes IUsed IFree IUse%% Mounted on\n'
		printf '/dev/sda1 65536 %s %s %s%% /\n' $((pct*100)) $((65536-pct*100)) "$pct"
	} | XYMONHOME="$LIVE" ./client/xymon 127.0.0.1 "@" 2>>"$LIVE/soak.log"

	# marker status: units + thresholds; one steady instance, one that
	# changes value at cycle 30
	local lz2=5; [ "$c" -ge 30 ] && lz2=9
	{
		printf 'status %s.soakm green marker metrics\n' "$h"
		printf '<!--XYMON METRICS: soakm\n'
		printf 'DS:val:GAUGE:600:0:U:ms DS:val_warn:GAUGE:600:0:U:ms\n'
		printf 'THRESHOLD:val:>val_warn:warn\n'
		printf 'a %s:%s\n' $((c % 40 + 10)) 45
		printf -- '-->\n'
		printf '<!--XYMON METRICS: soakst\n'
		printf 'DS:v:GAUGE:600:0:U\n'
		printf 'steady 7\n'
		printf 'wakes %s\n' "$lz2"
		printf -- '-->\n'
	} | XYMONHOME="$LIVE" ./client/xymon 127.0.0.1 "@" 2>>"$LIVE/soak.log"
}

check() {  # per-cycle invariants
	local h idx fn
	for p in xymond ch-client ch-rrd; do
		if ! kill -0 "$(cat "$LIVE/$p.pid" 2>/dev/null)" 2>/dev/null; then
			anomaly "$p not running"
			return
		fi
	done
	if grep -lE 'buffer overflow|Assertion|Segmentation|AddressSanitizer' "$LIVE"/log/*.log >/dev/null 2>&1; then
		anomaly "crash marker in logs: $(grep -hE 'buffer overflow|Assertion|Segmentation' "$LIVE"/log/*.log | head -1)"
	fi
	for h in $HOSTS; do
		idx="$LIVE/data/rrd/$h/.fileset-index"
		[ -d "$LIVE/data/rrd/$h" ] || continue
		if [ -f "$idx" ]; then
			# every entry has its file; every file its entry
			while read -r fn rest; do
				case "$fn" in \#*|'') continue ;; esac
				[ -f "$LIVE/data/rrd/$h/$fn" ] || anomaly "$h: index entry $fn has no file"
			done <"$idx"
			for f in "$LIVE/data/rrd/$h"/*.rrd; do
				[ -e "$f" ] || continue
				grep -q "^$(basename "$f") " "$idx" || anomaly "$h: file $(basename "$f") not in index"
			done
		fi
	done
	# RSS sample of the rrd worker (child of the channel)
	local rpid rss
	rpid=$(pgrep -P "$(cat "$LIVE/ch-rrd.pid" 2>/dev/null)" 2>/dev/null | head -1)
	[ -n "${rpid:-}" ] && { rss=$(awk '/VmRSS/{print $2}' "/proc/$rpid/status" 2>/dev/null); echo "$(date +%s) $rss" >>"$LIVE/rss.log"; }
}

note "soak start: $CYCLES cycles x ${INTERVAL}s, home $LIVE"
c=0
while [ "$c" -lt "$CYCLES" ]; do
	c=$((c+1))
	for h in $HOSTS; do send_host "$h" "$c"; done
	check
	if [ $((c % 20)) -eq 0 ]; then
		note "cycle $c: restarting the rrd worker"
		kill "$(cat "$LIVE/ch-rrd.pid")" 2>/dev/null; sleep 1; start_rrd; sleep 1
	fi
	if [ $((c % 50)) -eq 0 ]; then
		note "cycle $c: HUP xymond"
		kill -HUP "$(cat "$LIVE/xymond.pid")" 2>/dev/null
	fi
	sleep "$INTERVAL"
done

# final report
# (grep -c prints its 0 AND exits nonzero on no match - no || echo here)
nanom=$(grep -c . "$ANOM" 2>/dev/null); nanom=${nanom:-0}
first_rss=$(head -1 "$LIVE/rss.log" 2>/dev/null | awk '{print $2}')
last_rss=$(tail -1 "$LIVE/rss.log" 2>/dev/null | awk '{print $2}')
note "soak done: $c cycles, anomalies=$nanom, rrd-worker RSS ${first_rss:-?}kB -> ${last_rss:-?}kB"
echo "SOAK RESULT: cycles=$c anomalies=$nanom rss=${first_rss:-?}->${last_rss:-?}kB home=$LIVE"
[ "$nanom" -eq 0 ]
