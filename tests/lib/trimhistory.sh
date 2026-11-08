# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash
#
# tests/lib/trimhistory.sh -- shared build/fixture recipe for the trimhistory
# tests. Source after assert.sh, then:
#
#     setup_trimhistory "$work"
#     hosts_cfg "$work" '127.0.0.1 active.example.com # conn'
#     seed_hosthistory "$work" active.example.com
#     seed_svchistory  "$work" active,example,com.conn
#     seed_histlogs    "$work" active_example_com conn
#     run_trimhistory  "$work" --drop
#
# HOSTSCFG carries a "!" prefix on purpose. load_hostnames() asks xymond for
# the configuration whenever the filename it is handed equals $HOSTSCFG
# (lib/loadhosts_file.c) -- which is exactly what trimhistory passes it. With
# no xymond listening the host list then loads empty, every history file looks
# orphaned, and a --drop test would "pass" by deleting the entire fixture.
# The "!" forces the file load these tests are about.

[ -n "${__XYMON_TESTS_TRIMHISTORY_SOURCED:-}" ] && return 0
__XYMON_TESTS_TRIMHISTORY_SOURCED=1

# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "${BASH_SOURCE[0]}")/build-worker.sh"

# Each fixture file carries three records: two before the cutoff and one after
# it. Two, because trim_history() keeps the last pre-cutoff line on purpose --
# it is the state the host was in when the cutoff passed -- so a file seeded
# with a single old line looks untrimmed however well the trimming works.
# TRIM_ANCIENT is therefore the line that must disappear, TRIM_OLD the one
# that must survive, TRIM_RECENT the line after the cutoff. Having both a kept
# and a dropped line also means a deleted file can never be mistaken for a
# trimmed one.
TRIM_NOW=$(date +%s)
TRIM_CUTOFF=$((TRIM_NOW - 86400))
TRIM_ANCIENT=$((TRIM_NOW - 8640000))
TRIM_OLD=$((TRIM_NOW - 864000))
TRIM_RECENT=$((TRIM_NOW - 3600))

# A service-history record carries the change time twice: as text and as the
# epoch the trimmer actually reads (column 7). The text is never parsed there,
# so the fixtures use one fixed, obviously-old spelling rather than formatting
# an epoch -- converting one portably means date -d on GNU and date -r on the
# BSDs, and the suite runs on both.
TRIM_TEXTDATE='Mon Jan  1 00:00:00 2001'

# A histlog file name, by contrast, *is* parsed: logtime() wants exactly
# WWW_MMM_DD_hh:mm:ss_YYYY, 24 characters. This one is the same instant, and
# comfortably before any cutoff a test can pick.
TRIM_LOGSTAMP='Mon_Jan__1_00:00:00_2001'

setup_trimhistory() {
	local work=$1
	build_xymond_worker "$work" trimhistory xymond/trimhistory.c
	mkdir -p "$work/etc" "$work/var/hist" "$work/var/histlogs" "$work/var/logs"
	: >"$work/etc/hosts.cfg"
}

# hosts_cfg WORK LINE... -- append raw lines to the fixture hosts.cfg.
hosts_cfg() {
	local work=$1
	shift
	printf '%s\n' "$@" >>"$work/etc/hosts.cfg"
}

# seed_hosthistory WORK NAME -- a host-history file, as xymond_history writes
# it: "testname tstamp lastchg duration newcol oldcol trend" (the trimmer
# reads the timestamp from column 2).
seed_hosthistory() {
	local work=$1 name=$2
	{
		printf 'conn %d %d 3600 purple red 0\n' "$TRIM_ANCIENT" "$((TRIM_ANCIENT - 3600))"
		printf 'conn %d %d 3600 red green 0\n' "$TRIM_OLD" "$((TRIM_OLD - 3600))"
		printf 'conn %d %d 3600 green red 0\n' "$TRIM_RECENT" "$((TRIM_RECENT - 3600))"
	} >"$work/var/hist/$name"
}

# seed_svchistory WORK NAME -- a service-history file: "color <ctime> tstamp
# duration nextcolor" (the trimmer reads the timestamp from column 7).
seed_svchistory() {
	local work=$1 name=$2
	{
		printf 'purple %s %d 3600 red\n' "$TRIM_TEXTDATE" "$TRIM_ANCIENT"
		printf 'red %s %d 3600 green\n' "$TRIM_TEXTDATE" "$TRIM_OLD"
		printf 'green %s %d 3600 red\n' "$TRIM_TEXTDATE" "$TRIM_RECENT"
	} >"$work/var/hist/$name"
}

# seed_histlogs WORK LOGNAME SVC -- one status log older than the cutoff.
# LOGNAME is the hostname with dots and commas as underscores; the file name
# is the 24-character stamp logtime() parses (WWW_MMM_DD_hh:mm:ss_YYYY).
seed_histlogs() {
	local work=$1 logname=$2 svc=$3
	mkdir -p "$work/var/histlogs/$logname/$svc"
	printf 'green %s stale\n' "$TRIM_TEXTDATE" \
		>"$work/var/histlogs/$logname/$svc/$TRIM_LOGSTAMP"
}

# run_trimhistory WORK ARGS... -- run against the fixture, capturing stderr in
# <work>/trim.log and printing it, so a caller can grep the run's messages.
#
# The file is replaced on every run, not added to: a test that runs twice
# against the same fixture would otherwise grep the first run's messages and
# see them as the second's.
#
# A caller that is testing the configuration load itself sets TRIM_HOSTSCFG to
# the spelling it wants (a bare path takes the xymond-first route).
#
# XYMONSERVERLOGS is redirected for the same reason as the rest, and it is the
# one that reaches outside the fixture if it is not: after trimming
# "allevents", trimhistory reads $XYMONSERVERLOGS/xymond_history.pid and sends
# that process a SIGHUP (xymond/trimhistory.c). Left unset it falls back to the
# compiled-in XYMONLOGDIR, so a test that seeds an allevents file would signal
# the xymond_history of a real Xymon running on the machine -- and pass.
run_trimhistory() {
	local work=$1 rc
	shift
	set +e
	XYMONHOME="$work" \
	HOSTSCFG="${TRIM_HOSTSCFG:-!$work/etc/hosts.cfg}" \
	XYMONHISTDIR="$work/var/hist" \
	XYMONHISTLOGS="$work/var/histlogs" \
	XYMONSERVERLOGS="$work/var/logs" \
	XYMONTMP="$work" \
		"$work/trimhistory" --cutoff="$TRIM_CUTOFF" "$@" >"$work/trim.log" 2>&1
	rc=$?
	set -e
	cat "$work/trim.log"
	return $rc
}
