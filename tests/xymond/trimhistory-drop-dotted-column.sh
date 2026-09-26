#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-dotted-column.sh
#
# A service-history file is named "<hostname with dots as commas>.<column>",
# and trimhistory recovers the two halves by splitting on the *last* dot
# (xymond/trimhistory.c:458). A column name may itself contain a dot -- the
# web side treats that as ordinary and tests/web/svcstatus-histlog-serving.sh
# serves a "web.grp" histlog -- and for those files the split lands inside the
# column name instead of before it:
#
#     dotted,example,com.web.grp  ->  host "dotted.example.com.web", test "grp"
#
# No such host exists, so --drop deletes the file as an orphan even though its
# real host is listed in hosts.cfg. Same symptom as #281 (a listed host loses
# its history) reached by a different mechanism, and independent of any
# NOTBEFORE:/NOTAFTER: window.
#
# A comma inside a hostname is deliberately not tested: hosts.cfg(5) states a
# canonical name cannot contain one, because the web URLs use a comma to
# encode a period (#309/#314), so the loss in that case follows policy.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"
hosts_cfg "$work" \
	'127.0.0.1 dotted.example.com # conn' \
	'127.0.0.1 plainname # conn' \
	'127.0.0.1 gone # conn' \
	'127.0.0.1 sched.example.com # conn NOTAFTER:202001010000'

seed_svchistory "$work" dotted,example,com.conn      # single-word column
seed_svchistory "$work" dotted,example,com.web.grp   # column name with a dot
seed_svchistory "$work" dotted,example,com.a.b.c     # ... and with several
seed_svchistory "$work" plainname.web.grp            # same, host without dots
seed_svchistory "$work" sched,example,com.web.grp    # dotted column AND out of period
seed_svchistory "$work" gone,example,com.web.grp     # absent host, still an orphan
seed_hosthistory "$work" dotted.example.com

# The ambiguous shape: "gone.example.com" can be read as a host-history file
# for a host of that name, or as service "example.com" of the listed host
# "gone". The host it belongs to has been removed from hosts.cfg, so it is an
# orphan -- and only its contents say so: a host-history record carries the
# timestamp in column 2, a service record a weekday name there.
seed_hosthistory "$work" gone.example.com

run_trimhistory "$work" --drop >"$work/out.log" 2>&1 || true
log=$(cat "$work/out.log")

# Sanity: the ordinary case works, so a failure below is about the dot in the
# column name and nothing else.
assert_file_exists "$work/var/hist/dotted,example,com.conn" \
	"--drop deleted a service-history file with a single-word column name"

assert_file_exists "$work/var/hist/dotted,example,com.web.grp" \
	"--drop deleted the history of column web.grp on a listed host: the name was split at the last dot, not the column boundary"
assert_file_exists "$work/var/hist/plainname.web.grp" \
	"--drop deleted the history of column web.grp on a listed host whose name has no dots"
assert_file_exists "$work/var/hist/dotted,example,com.a.b.c" \
	"--drop deleted the history of a column whose name holds several dots"

# Both mechanisms at once: a dotted column on a host that is listed but
# outside its window has to survive each of them.
assert_file_exists "$work/var/hist/sched,example,com.web.grp" \
	"--drop deleted a dotted column of a scheduled-out host (#281 plus the split)"

# A stale host-history file whose first label happens to be a listed host is
# still an orphan. Read as a service file it would also be trimmed on the wrong
# column -- column 7 of a host-history line is the trend, so everything but the
# last line goes.
[ ! -e "$work/var/hist/gone.example.com" ] \
	|| { echo "$log" >&2; fail "--drop kept gone.example.com, whose host was removed from hosts.cfg -- it was read as service 'example.com' of the listed host 'gone'"; }

# The feature still has to work: an absent host is an orphan whatever its
# column is called.
[ ! -e "$work/var/hist/gone,example,com.web.grp" ] \
	|| { echo "$log" >&2; fail "--drop kept a dotted-column file whose host is not in hosts.cfg"; }

# ---- and without --drop, an orphan is reported, never rewritten ------------
# Misreading the same name as a service file trims it on the service column,
# which silently truncates a file that this mode must not touch at all.
work2=$(mktempdir)
setup_trimhistory "$work2"
hosts_cfg "$work2" '127.0.0.1 gone # conn'
seed_hosthistory "$work2" gone.example.com
before=$(cat "$work2/var/hist/gone.example.com")

run_trimhistory "$work2" >"$work2/nodrop.log" 2>&1 || true

assert_equal "$before" "$(cat "$work2/var/hist/gone.example.com")" \
	"a run without --drop rewrote an orphaned host-history file"

echo "OK $(basename "$0")"
