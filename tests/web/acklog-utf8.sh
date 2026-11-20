#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/acklog-utf8.sh
#
# do_acklog() (lib/acklog.c) shows the first 30 bytes of an acknowledgement
# message in the non-green page's ack table. It cut with "ackmsg[30] = 0",
# which lands inside a UTF-8 character when byte 30 is a continuation byte, so
# the rendered cell ended in a lone lead byte -- invalid UTF-8 on a page served
# as UTF-8. The cut now backs up over a trailing continuation byte.
#
# The message here is 29 'a's, then "e'" as the two UTF-8 bytes C3 A9, then
# "zzz". Byte 30 (0-indexed) is the A9 continuation byte: the old cut kept the
# C3 and dropped the A9, leaving a dangling C3; the fix drops the whole
# character, so no C3 byte reaches the output.
#
# A second message, on another test, is "lat" then 48 bytes B0 (Latin-1
# degree signs, each in the continuation range). A UTF-8 character has at most
# three continuation bytes, so the cut backs up to byte 27 and no further: the
# cell is "lat" and exactly 24 B0 bytes, checked up to the cell's closing tag.
# Followed back without a bound, the run would leave only "lat"; a bound that
# stops short of 27 would keep more, and split a four-byte UTF-8 character
# whose lead byte is at 27.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-harness.sh
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymon.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
mkdir -p "$work/logs" "$work/acks"

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a libxymon.a

printf '1.2.3.4\tackhost\t# cpu disk\n' > "$work/hosts.cfg"

pfx=$(printf '%029d' 0 | tr 0 a)          # 29 'a' bytes
eacute=$(printf '\303\251')               # one UTF-8 character, bytes C3 A9
lead=$(printf '\303')                     # its lead byte alone
now=$(date +%s)
# One acklog record: a huge duration so it never reads as expired, acktime
# "now" so it falls inside do_acklog's 240-minute window, host "ackhost.cpu"
# so hostinfo() resolves it. The message is the rest of the line after the
# seventh TAB.
printf '%s\t1\t100000\t1\tnp_filename_not_used\tackhost.cpu\tgreen\t%szzz\n' \
	"$now" "${pfx}${eacute}" > "$work/logs/acknowledge.log"
degree=$(printf '\260')                    # Latin-1 degree sign, byte B0
run8="$degree$degree$degree$degree$degree$degree$degree$degree"
run24="$run8$run8$run8"
printf '%s\t2\t100000\t2\tnp_filename_not_used\tackhost.disk\tgreen\tlat%s%s\n' \
	"$now" "$run24" "$run24" >> "$work/logs/acknowledge.log"

build_harness "$work" "$work/harness" \
	"$here/acklog-utf8-harness.c" libxymon.a libxymoncomm.a libxymon.a

# The "!" prefix forces load_hostnames() to read the file instead of asking a
# xymond that is not running (lib/loadhosts_file.c).
XYMONSERVERLOGS="$work/logs" HOSTSCFG="!$work/hosts.cfg" XYMONACKDIR="$work/acks" \
	"$work/harness" "!$work/hosts.cfg" > "$work/out" 2>"$work/err" \
	|| fail "the harness failed: $(cat "$work/err")"

grep -aqF "$pfx" "$work/out" \
	|| fail "the acknowledgement message did not render, so the cut was not exercised:
$(cat "$work/out")"

if grep -aqF "$lead" "$work/out"; then
	fail "the 30-byte cut split a UTF-8 character: a lone lead byte (C3) reached
the ack table. The cut must back up to a character boundary."
fi

grep -aqF "lat$run24</TD>" "$work/out" \
	|| fail "a run of Latin-1 bytes in 0x80-0xBF was not cut at byte 27:
the cell should be 'lat' and exactly 24 B0 bytes."

pass "do_acklog's 30-byte message cut does not split a UTF-8 character, and backs up at most three bytes"
