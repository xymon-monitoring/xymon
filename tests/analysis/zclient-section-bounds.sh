#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/zclient-section-bounds.sh
#
# The z/VM, z/VSE and z/OS handlers in xymond/client/ build their lines in
# fixed arrays and read their figures out of the sections of the report.
# This drives the real xymond_client in --local --no-update mode (statuses go
# to stdout), one report per process, and checks the emitted text:
#
#   - a section copied into a fixed array is cut to the array's size: 99
#     bytes of [uptime] in the cpu status, 66 bytes of a marker-less [paging]
#     after its 33-byte text, a getvis job name to 8 characters and a
#     partition to 3, a getvis row to 127 bytes, a CICS row to 99, the z/OS
#     maxuser text to 255, the z/VM [date] to the cpu line's 1023 bytes and
#     the paging line's 255, and a z/VM user name, a z/VSE or z/OS [cpu] or
#     [paging], and the [date] in every z/VSE and z/OS status to their
#     line's 4095 bytes. A copy that is no longer cut fails here on any
#     build, whether or not the compiler's own checks would stop it;
#   - a section that does not hold the figures it should -- no marker, no
#     number -- is read as zeroes: the status is emitted with 0 where the
#     figures would be, a percentage of anything out of 0 as 0, no "nan", no
#     figure from an earlier line, and no "recently rebooted" from an
#     [uptime] that gave no time. A line with no word at all is no CICS
#     system and no getvis partition, and is skipped; a [cics] of such lines
#     only still gets its own status, once, and the status before it is not
#     sent again;
#   - what a real client sends is carried whole: the longest valid getvis word
#     ("Z1-CICSICCF", a 2-character partition, "-", an 8-character job name)
#     is read with its own figures; a z/VM INDICATE LOAD response longer than
#     both the cpu line's 1024 bytes and the paging line's 256, in the format
#     the code comments give, ends both statuses; a ps line of 5000 bytes is
#     carried whole on z/VSE and z/OS; and a z/VM user list of fewer than
#     four names is padded with blanks.
#
# Not pinned by the output, because what they replace happened to print the
# same on the builds tried: the zero start of the z/OS maxuser figures and of
# the CPU load (all three), the z/VM paging rate and MDC reads, the uptime
# check (z/VSE without fortify; z/VM on some runs and not others), and the
# one byte jinfo[] gained. A memory checker shows them; this test does not.
#
# Not reached: the [date] in the z/VM procs status, and four z/VM user names
# that together fill the line.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
printf 'DEFAULT\n' > "$work/analysis.cfg"

long=$(awk 'BEGIN { while (n++ < 3000) printf "A" }')
# Long enough that a getvis word read without a width changes the output,
# whatever the layout of the function's arrays.
long9k=$(awk 'BEGIN { while (n++ < 9000) printf "A" }')
a99=$(awk 'BEGIN { while (n++ < 99) printf "A" }')
a66=$(awk 'BEGIN { while (n++ < 66) printf "A" }')
b4096=$(awk 'BEGIN { while (n++ < 4096) printf "B" }')
b5000=$(awk 'BEGIN { while (n++ < 5000) printf "B" }')
blank="   "	# a line of blanks, kept out of the source so git diff --check stays quiet
# 30 processor lines: a large guest's INDICATE LOAD runs past 1024 bytes.
procs=$(awk 'BEGIN { for (n = 0; n < 60; n += 2) printf "PROC %04d-038%% CP        PROC %04d-038%% CP\n", n, n+1 }')
zerorow='0         0         0          0          0          0      0        0'
date='Tue Nov 25 07:00:00 UTC 2026'

# runcase NAME OS REPORT-SECTIONS
# Sends one client report for host NAME with OS and the [date] in $date,
# prints what it emitted.
runcase() {
	printf '@@client|1755640000|127.0.0.1|%s|%s|%s|\n[date]\n%s\n%s\n@@\n' \
		"$1" "$2" "$2" "$date" "$3" | env \
		XYMONHOME="$work/home" \
		XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
		2>"$work/stderr.log" \
		|| fail "$1 ($2): xymond_client did not complete: $(tail -n 3 "$work/stderr.log")"
}

# check NAME OS WANT REPORT: WANT must appear in what the report emitted,
# which is left in $out for the caller's further assertions.
check() {
	out=$(runcase "$1" "$2" "$4")
	assert_contains "$3" "$out" "$1 ($2): '$3' was not emitted"
}

for os in zvm zvse zos; do
	case $os in
	  zvm)  cpu='AVGPROC-005%' ;;
	  zvse) cpu='Avg CPU=005%' ;;
	  zos)  cpu='CPU Utilization 005%' ;;
	esac

	# A long [uptime] is cut to myupstr[100]: 99 bytes of it, not one more.
	# It gives no time, so nothing is said about a reboot.
	check "up$os" $os "status up$os.cpu green" "[uptime]
$long
[cpu]
$cpu"
	assert_contains "$a99 $cpu" "$out" "up$os: the uptime text was not cut to 99 bytes"
	assert_not_contains "A$a99" "$out" "up$os: more than 99 bytes of [uptime] were copied"
	assert_not_contains "Machine" "$out" "up$os: an [uptime] that gave no time was read as one"

	# A [cpu] without its marker reports no load, and is green.
	check "cpu$os" $os "status cpu$os.cpu green" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
nothing to read here"
done

# z/VM reads its paging and MDC figures from [cpu] too. Each case has every
# marker but the one it checks, so it reaches that one.
check "pgzvm" zvm "status pgzvm.paging green" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
AVGPROC-005% READS-000001/SEC WRITES-000001/SEC HIT RATIO-098%"
check "wrzvm" zvm "data wrzvm.mdc
zvm
1:0:98" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
AVGPROC-005% PAGING-0001/SEC READS-000001/SEC HIT RATIO-098%"
check "rtzvm" zvm "data rtzvm.mdc
zvm
1:1:0" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
AVGPROC-005% PAGING-0001/SEC READS-000001/SEC WRITES-000001/SEC"

# z/VM: an INDICATE LOAD response longer than the cpu line's msgline[1024]
# and the paging line's msgline[256] is carried whole, in both statuses.
check "indzvm" zvm "status indzvm.paging" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
AVGPROC-038% 60
XSTORE-000021/SEC MIGRATE-0001/SEC
MDC READS-000068/SEC WRITES-000001/SEC HIT RATIO-099%
PAGING-0001/SEC STEAL-000%
Q0-00006(00000)                           DORMANT-00357
Q1-00001(00000)           E1-00000(00000)
Q2-00001(00000) EXPAN-002 E2-00000(00000)
Q3-00034(00000) EXPAN-002 E3-00000(00000)
$procs
LIMITED-00000"
assert_equal 2 "$(printf '%s\n' "$out" | grep -c 'LIMITED-00000$')" \
	"indzvm: the end of [cpu] is not in both the cpu and the paging status"

# z/VM user list: fewer than four names are padded with blanks, and a name
# longer than the line is cut to it.
check "uszvm" zvm "status uszvm.procs" "[UserID]
ONE
TWO
THREE"
assert_contains "
ONE      TWO      THREE            " "$out" "uszvm: a three-name user list was not padded with blanks"
check "unzvm" zvm "status unzvm.procs" "[UserID]
$b5000"
assert_not_contains "$b4096" "$out" "unzvm: a user name was not cut to its line's 4095 bytes"

# z/VSE and z/OS: a [paging] without its marker is quoted, cut to
# pagingresult[100]: 66 bytes of it after the 33 of the text before it.
for os in zvse zos; do
	check "pg$os" $os "Can not find page rate value in:
$a66 A" "[paging]
$long"
done

# z/VSE [cpu] and z/OS [date] longer than their line are cut to it.
check "cpbzvse" zvse "status cpbzvse.cpu" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
Avg CPU=005% $b5000"
assert_not_contains "$b4096" "$out" "cpbzvse: [cpu] was not cut to its line's 4095 bytes"
date=$b5000 check "dtzos" zos "status dtzos.paging" "[paging]
Paging Rate 1.0"
assert_not_contains "$b4096" "$out" "dtzos: [date] was not cut to its line's 4095 bytes"

# z/VSE: a ps line of 5000 bytes is carried whole.
check "jbzvse" zvse "status jbzvse.procs" "[jobs]
$b5000"
assert_contains "
$b5000" "$out" "jbzvse: a 5000-byte ps line was not carried whole"

# z/VSE getvis: a line with no word, the longest valid word (a 2-character
# partition, "-", an 8-character job name), an over-long job name, and an
# over-long partition word. A line with no word is skipped; a line whose
# figures were not read shows zeroes, not the figures of the line before it.
check "gvzvse" zvse "status gvzvse.getvis" "[getvis]
Partition    Used/24  Free/24  Used/Any  Free/Any
F1               824      264       824       264
$blank
Z1-CICSICCF     7160     3844     27516      4992
-ABCDEFGHIJKLMNOPQRSTUVWXYZ 100 100 100 100
$long9k 100 100 100 100"
assert_contains "
F1  -            1088       824       264       1088        824        264     76       76
Z1  CICSICCF    11004      7160      3844      32508      27516       4992     65       85
" "$out" "gvzvse: a line with no word was reported, or an 11-character partition-and-job word was not read whole"
assert_contains "
-A  ABCDEFGH        $zerorow
" "$out" "gvzvse: the job name was not cut to 8 characters, with zeroes for its unread figures"
assert_not_contains "ABCDEFGHI" "$out" "gvzvse: more than 8 characters of a job name were copied"
assert_contains "
AAA -               $zerorow" "$out" "gvzvse: the partition was not cut to 3 characters, with zeroes for its unread figures"
assert_not_contains "AAAA -" "$out" "gvzvse: more than 3 characters of a partition were copied"

# z/VSE and z/OS CICS: a blank line is no system: not the previous system
# again, and not a row of its own. A system without figures reads as zeroes,
# not the previous system's figures.
for os in zvse zos; do
	check "cics$os" $os "status cics$os.cics green" "[cics]
CICS1 2026/11/25 07:00:00 5 10.0 20.0
$blank
CICS2 2026/11/25 07:00:00 6 1.0 2.0
CICS3"
	assert_contains "
CICS1         5       10.0        20.0
CICS2         6       1.0        2.0
CICS3         0       0.0        0.0" "$out" "cics$os: a blank line was reported as a system, or a system without figures took the previous one's"
	# A row whose figures are too large for cicsresult[100] is cut to its 99
	# bytes. How 3e38 prints depends on the C library; the length does not.
	check "lencics$os" $os "status lencics$os.cics" "[cics]
C1 d t 5 3e38 3e38"
	n=$(printf '%s\n' "$out" | awk '/^C1 / { print length($0) }')
	[ "${n:-0}" -gt 0 ] && [ "$n" -le 99 ] \
		|| fail "lencics$os: a CICS row of $n bytes, not cut to cicsresult's 99"
done

# z/VSE and z/OS CICS: a section of blank lines only is no system at all:
# the cics status is still emitted, and once.
for os in zvse zos; do
	check "cicsbl$os" $os "status cicsbl$os.cics green" "[paging]
Paging Rate 1.0
[cics]
$blank"
	assert_equal 1 "$(printf '%s\n' "$out" | grep -o "status cicsbl$os\\.paging" | wc -l | tr -d ' ')" \
		"cicsbl$os: a [cics] of blank lines sent the paging status again"
	assert_equal 1 "$(printf '%s\n' "$out" | grep -o "status cicsbl$os\\.cics" | wc -l | tr -d ' ')" \
		"cicsbl$os: a [cics] of blank lines did not get exactly one cics status"
done

# z/VM: the [date] text is cut to the cpu line's 1023 bytes and the paging
# line's 255, before the rest of the line is added.
d1100=$(awk 'BEGIN { while (n++ < 1100) printf "D" }')
date=$d1100 check "dtzvm" zvm "status dtzvm.paging" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
AVGPROC-005%"
for svc in cpu paging; do
	n=$(printf '%s\n' "$out" | awk -v s="status dtzvm.$svc " 'index($0, s) { print gsub(/D/, "D") }')
	case $svc in cpu) max=1023 ;; paging) max=255 ;; esac
	[ "${n:-0}" -gt 0 ] && [ "$n" -lt "$max" ] \
		|| fail "dtzvm: the $svc status carried $n bytes of [date], not fewer than its line's $max"
done

# z/VSE [paging] and z/OS [cpu] longer than their line are cut to it, and a
# z/OS ps line of 5000 bytes is carried whole, as the z/VSE one is.
check "pbzvse" zvse "status pbzvse.paging" "[paging]
Page Rate=1 $b5000"
assert_not_contains "$b4096" "$out" "pbzvse: [paging] was not cut to its line's 4095 bytes"
check "cpbzos" zos "status cpbzos.cpu" "[uptime]
Uptime: 1 Days, 2 Hours, 3 Minutes
[cpu]
CPU Utilization 005% $b5000"
assert_not_contains "$b4096" "$out" "cpbzos: [cpu] was not cut to its line's 4095 bytes"
check "jbzos" zos "status jbzos.procs" "[jobs]
$b5000"
assert_contains "
$b5000" "$out" "jbzos: a 5000-byte ps line was not carried whole"

# z/VSE getvis: figures that make a row longer than getvisresult[128] are
# cut to its 127 bytes.
check "gvbzvse" zvse "status gvbzvse.getvis" "[getvis]
Partition    Used/24  Free/24  Used/Any  Free/Any
F1 -2000000000 2000000001 -2000000000 2000000001"
n=$(printf '%s\n' "$out" | awk '/^F1 / { print length($0) }')
[ "${n:-0}" -gt 0 ] && [ "$n" -le 127 ] \
	|| fail "gvbzvse: a getvis row of $n bytes, not cut to getvisresult's 127"

# z/VSE [memory] and [nparts] that hold no figures: zeroes, and green.
check "memzvse" zvse "status memzvse.memory green" "[memory]
nothing to read here"
assert_contains "z/VSE VSIZE Utilization 0.0%
Memory Allocated 0K, Memory Available 0K" "$out" "memzvse: figures were reported that the report did not hold"
check "npzvse" zvse "status npzvse.nparts green" "[nparts]
nothing to read here"
assert_contains "Nparts:        0  Free:        0  Used:        0  0.0" "$out" "npzvse: figures were reported that the report did not hold"

# z/OS [memory] and [maxuser] without any of their markers: zeroes, and green.
check "memzos" zos "status memzos.memory green" "[memory]
nothing to read here"
for area in 'CSA ' 'ECSA' 'SQA ' 'ESQA'; do
	assert_contains " $area        0        0        0   0.0" "$out" "memzos: $area figures were reported that the report did not hold"
done
assert_not_contains "nan" "$out" "memzos: a utilisation of 0 out of 0 was printed as nan"
check "muzos" zos "status muzos.maxuser green" "[maxuser]
nothing to read here"
assert_contains " Maxuser:        0  Free:        0  Used:        0" "$out" "muzos: figures were reported that the report did not hold"

# z/OS [maxuser] with none configured but some in use: a percentage of
# anything out of 0 is 0, not inf.
check "mu0zos" zos "status mu0zos.maxuser green" "[maxuser]
Maxusers: 0 Free: 5
RSVTSTRT: 0 Free: -3
RSVNONR: 0 Free: 3"
assert_contains " Maxuser:        0  Free:        5  Used:       -5  0.0
RSVTSTRT:        0  Free:       -3  Used:        3  0.0
 RSVNONR:        0  Free:        3  Used:       -3  0.0" "$out" "mu0zos: a utilisation out of 0 was not reported as 0"
assert_not_contains "inf" "$out" "mu0zos: a utilisation out of 0 was printed as inf"
# Figures too large for maxustr[256]: the maxuser text is cut to its 255
# bytes. The figures keep it green, so no alert line follows the text.
check "lenmuzos" zos "status lenmuzos.maxuser" "[maxuser]
Maxusers: -9000000000000000000 Free: -4500000000000000000
RSVTSTRT: -9000000000000000000 Free: -4500000000000000000
RSVNONR: -9000000000000000000 Free: -4500000000000000000"
n=$(printf '%s\n' "$out" | awk '/^ Maxuser:/ { f = 1 } f && /^ *status / { f = 0 } f { n += length($0) + 1 } END { print n + 0 }')
[ "$n" -gt 0 ] && [ "$n" -le 256 ] \
	|| fail "lenmuzos: a maxuser text of $n bytes, not cut to maxustr's 255"

# z/VSE and z/OS: every other status line that carries [date] cuts it to its
# 4095 bytes.
date=$b5000 check "dtazvse" zvse "status dtazvse.nparts" "[cics]
CICS1 d t 5 1.0 2.0
[jobs]
J1
[memory]
1000 500
[getvis]
Partition    Used/24  Free/24  Used/Any  Free/Any
F1 824 264 824 264
[nparts]
44 12"
assert_not_contains "$b4096" "$out" "dtazvse: a status line did not cut [date] to its 4095 bytes"
date=$b5000 check "dtazos" zos "status dtazos.maxuser" "[cics]
CICS1 d t 5 1.0 2.0
[jobs]
J1
[memory]
CSA 10 5 5
[maxuser]
Maxusers: 250 Free: 100"
assert_not_contains "$b4096" "$out" "dtazos: a status line did not cut [date] to its 4095 bytes"

pass "the z/VM, z/VSE and z/OS reports bound what they copy, and read a figure they cannot find as zero"
