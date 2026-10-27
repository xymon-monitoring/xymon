#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/top-darwin.sh
#
# The [top] section of the darwin client is the process table the cpu status
# page shows below the load line. macOS top prints thirty-five columns by
# default, 240 characters wide, most of them Mach counters that mean nothing
# on that page, where the Linux client's twelve fit in eighty. The client asks
# top for the seven the Linux table also has, in its order: pid, user, memory,
# state, %CPU, time, command (top's -stats option), and keeps the second of
# two samples, since the first carries no CPU figures.
#
# top is stubbed on PATH and does what the real one does with its arguments:
# with -stats it prints a header of those columns, without it the full header
# a real Apple-silicon Mac prints (2026-10); either way two samples, each
# opening with a "Processes:" line. sysctl is stubbed for the [nproc] line
# that precedes [top] in the same block, so the block runs on any platform.
# The block is cut out of the script and run with TOP and AWK pointing at the
# stub and the system awk, as xymonclient.sh would set them.
#
# Pinned here:
#   - the table header is the seven columns, in the Linux order
#   - none of the default-only columns (#PORTS, BOOSTS, CYCLES, ...) appear
#   - only the second sample is emitted
#   - the sort stays by CPU and the sample count stays at 20 rows

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SCRIPT="$ROOT/client/xymonclient-darwin.sh"
[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

work=$(mktempdir)
SNIPPET="$work/top-section.sh"
STUBS="$work/bin"
mkdir -p "$STUBS"

# The block from its comment to the closing fi, without the script's exit.
sed -n '/^# \$TOP must be set/,/^exit$/p' "$SCRIPT" | sed '$d' > "$SNIPPET"
grep -q 'echo "\[top\]"' "$SNIPPET" || fail "could not cut the [top] block out of $SCRIPT"

cat > "$STUBS/top" <<'EOF'
#!/bin/sh
# Records its arguments, prints two samples. The header follows -stats as the
# real top's does; the default header is the real one's.
printf '%s\n' "$*" > "$TOP_ARGS"
stats=""
while [ $# -gt 0 ]; do
	case $1 in
		-stats) stats=$2; shift ;;
	esac
	shift
done
if [ -n "$stats" ]; then
	header=""
	for key in $(printf '%s' "$stats" | tr ',' ' '); do
		case $key in
			pid) col=PID ;;
			user) col=USER ;;
			mem) col=MEM ;;
			state) col=STATE ;;
			cpu) col=%CPU ;;
			time) col=TIME ;;
			command) col=COMMAND ;;
			*) col=$(printf '%s' "$key" | tr '[:lower:]' '[:upper:]') ;;
		esac
		header="$header$col "
	done
else
	header='PID    COMMAND          %CPU TIME     #TH    #WQ #PORTS MEM    PURG  CMPRS PGRP  PPID  STATE    BOOSTS      %CPU_ME  %CPU_OTHRS UID FAULTS    COW  MSGSENT    MSGRECV    SYSBSD      SYSMACH     CSW         PAGEINS IDLEW   POWER INSTRS     CYCLES     JETPRI USER              #MREGS RPRVT VPRVT VSIZE KPRVT KSHRD'
fi
for sample in 1 2; do
	printf 'Processes: 885 total, 2 running, 883 sleeping, 3378 threads \n'
	printf '2026/10/27 09:00:1%s\n' "$sample"
	printf 'Load Avg: 1.95, 1.67, 1.58 \n'
	printf 'CPU usage: 3.41%% user, 3.18%% sys, 93.40%% idle \n'
	printf '\n%s\n' "$header"
	printf 'sample%s-row\n' "$sample"
done
EOF
cat > "$STUBS/sysctl" <<'EOF'
#!/bin/sh
echo 8
EOF
chmod +x "$STUBS/top" "$STUBS/sysctl"

TOP_ARGS="$work/top.args"
export TOP_ARGS
out=$(PATH="$STUBS:$PATH" TOP="$STUBS/top" AWK="$(command -v awk)" sh "$SNIPPET" 2>"$work/stderr.log") \
	|| fail "the [top] block exited non-zero: $(cat "$work/stderr.log")"
[ -s "$TOP_ARGS" ] || fail "top was not run: $out"

header=$(printf '%s\n' "$out" | grep '^PID' || true)
[ -n "$header" ] || fail "no process table header in the [top] section: $out"

# The seven columns of the Linux table, in its order, and nothing else.
assert_equal "PID USER MEM STATE %CPU TIME COMMAND" "$(printf '%s' "$header" | tr -s ' ' | sed 's/ $//')" \
	"the darwin [top] table is not pid, user, mem, state, cpu, time, command (top -stats)"
assert_not_contains "#PORTS" "$out" \
	"the darwin [top] table still carries top's default columns"

# Only the second sample reaches the section; the first has no CPU figures.
assert_equal 1 "$(printf '%s\n' "$out" | grep -c '^Processes:')" \
	"the [top] section does not hold exactly one top sample"
assert_contains "sample2-row" "$out" "the second top sample is missing from the section"
assert_not_contains "sample1-row" "$out" "the first top sample leaked into the section"

# Unchanged: two samples, 20 rows, sorted by CPU.
args=$(cat "$TOP_ARGS")
assert_match '(^| )-l 2( |$)' "$args" "top no longer takes two samples"
assert_match '(^| )-n 20( |$)' "$args" "top no longer limits the table to 20 rows"
assert_match '(^| )-o cpu( |$)' "$args" "top no longer sorts by CPU"

pass "xymonclient-darwin.sh [top]: the seven columns of the Linux table, from the second of two samples"
