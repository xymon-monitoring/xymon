#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/memory-darwin-rows.sh
#
# The darwin backend of xymond_client (xymond/client/darwin.c) fills all three
# rows of the memory status, as the Linux and BSD backends do, from the
# client's [meminfo] (vm_stat) and [swapusage] (sysctl vm.swapusage) sections:
#
#   Real/Physical   every page but the free ones, over every page vm_stat
#                   counts: free, active, inactive, speculative, wired, and
#                   the ones the compressor occupies
#   Actual/Virtual  active + wired + compressor pages, over the same total:
#                   the inactive and speculative pages are the file cache the
#                   kernel reclaims on demand, as "-/+ buffers/cache" leaves
#                   them out on Linux
#   Swap/Page       "used" over "total" of vm.swapusage, both in megabytes
#
# Pinned here, by driving the real xymond_client in --local --no-update mode
# (statuses print to stdout instead of being sent):
#
#   - a current vm_stat with a [swapusage] section yields the three rows with
#     the figures above, and MEMACT and MEMSWAP thresholds colour the second
#     and third, which they could not while the backend passed -1 for them
#   - a client without [swapusage], as every darwin client before this change
#     sends, keeps the first two rows and shows no Swap/Page row
#   - a vm_stat without the speculative and compressor lines (OS X 10.8 and
#     earlier) still yields the first two rows, with those pages counted as 0

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"

# 16 KiB pages: 64 pages to the megabyte. Host "modern": 1000M free, 2000M
# active, 3000M inactive, 100M speculative, 500M wired, 400M compressor, so
# 7000M in all, 6000M physical used (85%), 2900M actual (41%); swap 1536M of
# 2048M (75%). The thresholds below put actual at yellow and swap at red.
# Host "old": 4 KiB pages, four lines only, no swap section: 6500M in all,
# 5500M physical used (84%), 2500M actual (38%).
cat >"$work/analysis.cfg" <<'EOF'
MEMACT 30 95
MEMSWAP 50 70
EOF

out=$(printf '%s' '@@client|1755640000|127.0.0.1|modern.test|darwin|darwin|
[meminfo]
Mach Virtual Memory Statistics: (page size of 16384 bytes)
Pages free:                                    64000.
Pages active:                                 128000.
Pages inactive:                               192000.
Pages speculative:                              6400.
Pages throttled:                                   0.
Pages wired down:                              32000.
Pages purgeable:                               69790.
"Translation faults":                      995016442.
Pages stored in compressor:                    80000.
Pages occupied by compressor:                  25600.
Pageins:                                     7931203.
[swapusage]
vm.swapusage: total = 2048.00M  used = 1536.00M  free = 512.00M  (encrypted)
@@
@@client|1755640000|127.0.0.1|old.test|darwin|darwin|
[meminfo]
Mach Virtual Memory Statistics: (page size of 4096 bytes)
Pages free:                       256000.
Pages active:                     512000.
Pages inactive:                   768000.
Pages wired down:                 128000.
"Translation faults":          995016442.
Pageins:                         7931203.
@@
' | env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

modern=$(printf '%s\n' "$out" | sed -n '/^status modern,test\.memory /,/^$/p')
old=$(printf '%s\n' "$out" | sed -n '/^status old,test\.memory /,/^$/p')
[ -n "$modern" ] || fail "no memory status for modern.test in: $out"
[ -n "$old" ]    || fail "no memory status for old.test in: $out"

# Current vm_stat plus swapusage: three rows, each coloured by its own rule.
assert_match 'status modern,test\.memory red ' "$modern" \
	"the memory column did not go red on 75% swap against MEMSWAP 50 70"
assert_match '&green Real/Physical +6000M +7000M +85%' "$modern" \
	"Real/Physical does not count every non-free page over every page vm_stat lists"
assert_match '&yellow Actual/Virtual +2900M +7000M +41%' "$modern" \
	"Actual/Virtual is not active+wired+compressor pages, or MEMACT 30 95 did not colour it"
assert_match '&red Swap/Page +1536M +2048M +75%' "$modern" \
	"Swap/Page is not vm.swapusage used over total, or MEMSWAP 50 70 did not colour it"

# No [swapusage] section: the two memory rows stay, the swap row is absent.
assert_match 'status old,test\.memory yellow ' "$old" \
	"the memory column did not go yellow on 38% actual against MEMACT 30 95"
assert_match '&green Real/Physical +5500M +6500M +84%' "$old" \
	"Real/Physical changed for a vm_stat without the speculative and compressor lines"
assert_match '&yellow Actual/Virtual +2500M +6500M +38%' "$old" \
	"Actual/Virtual is missing or wrong for a vm_stat without the speculative and compressor lines"
assert_not_contains 'Swap/Page' "$old" \
	"a client without a [swapusage] section got a Swap/Page row"

pass "darwin memory status fills Real/Physical, Actual/Virtual and Swap/Page from vm_stat and vm.swapusage"
