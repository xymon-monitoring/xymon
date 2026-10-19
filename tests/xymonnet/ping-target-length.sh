#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Bruno Manzoni
#
# tests/xymonnet/ping-target-length.sh
#
# An extra conn= target, best or worst alike, too long for an address must
# not reach the ping tool, and must not cost another host its ping (#655,
# Debian #867341).
#
# xymonnet feeds the addresses to ping to the pinger's stdin, one per line,
# through a buffer of IP_ADDR_STRLEN+1 bytes. A target that did not fit lost
# its newline along with its tail, so the next address in sort order was
# glued onto it, was never pinged, and that unrelated host's conn went red.
#
# Both sides of the limit are checked, at the length include/libxymon.h sets:
# a target of IP_ADDR_STRLEN-1 characters is fed whole on its own line, one
# of IP_ADDR_STRLEN is skipped, logged and named in its host's status. The
# pinger here records its stdin and answers "alive" for every line, so no
# ICMP is needed: a host whose address arrives on its own line is green.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet

len=$(sed -n 's/^#define IP_ADDR_STRLEN \([0-9][0-9]*\).*/\1/p' "$root/include/libxymon.h")
[ -n "$len" ] || fail "IP_ADDR_STRLEN not found in include/libxymon.h"

# Both targets sort between 10.0.0.2 and 222.222.222.222, which is what an
# overlong one was glued to. The base is longer than any IP_ADDR_STRLEN.
base=2001:db8:dead:beef:1111:2222:3333:4444:5555:6666:7777:8888:9999:aaaa
fits=${base:0:$((len - 1))}
long=${base:0:$len}

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/home/tmp"

cat >"$work/fping" <<EOF
#!/bin/sh
while read ip; do echo "\$ip" >>"$work/fed"; echo "\$ip is alive"; done
exit 0
EOF
chmod +x "$work/fping"

cat >"$work/home/etc/hosts.cfg" <<EOF
10.0.0.1	first	# testip conn
10.0.0.2	longtarget	# testip conn=worst,$long
10.0.0.3	fittarget	# testip conn=worst,$fits
222.222.222.222	neighbour	# testip conn
EOF

out=$(XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
	FPING="$work/fping" FPINGOPTS="" \
	"$XYMONNET" --no-update --ping=conn --timeout=3 2>&1 || true)
fed=$(cat "$work/fed" 2>/dev/null || true)

assert_contains "$(printf '\n%s\n' "$fits")" "$(printf '\n%s\n' "$fed")" \
	"a target of $((len - 1)) characters, which fits, was not fed whole on its own line"
assert_contains "$(printf '\n222.222.222.222\n')" "$(printf '\n%s\n' "$fed")" \
	"the neighbour's address did not reach the pinger on a line of its own"
assert_not_contains "$long" "$fed" "a target of $len characters, too long for an address, was fed to the pinger"
assert_match 'status[^ ]* neighbour\.conn green' "$out" "the neighbour host lost its ping"
assert_contains "Host longtarget: ping target '$long' is too long" "$out" \
	"the skipped target was not logged with its host"
assert_not_contains "Host fittarget:" "$out" "a target that fits was reported as too long"
assert_contains "&red $long is too long for an IP address, not pinged" "$out" \
	"the skipped target is not named in its host's conn status"

pass "a conn=worst target of $len characters is skipped, logged and shown; one of $((len - 1)) is pinged, and the next host still is"
