#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/trimhistory-drop-classification.sh
#
# The contract around "trimhistory --drop": which history files it considers
# to belong to a known host, and which it removes. Written alongside the #281
# fix, which changes how trimhistory resolves a file's host, so the rest of
# that resolution is pinned rather than left to be discovered later:
#
#   - a host that is absent from hosts.cfg loses its files (that is the
#     feature), whether or not its name carries a dot;
#   - a host that is present keeps them, including when the file name differs
#     in case, when the name is a CLIENT: alias, and when it has no dots;
#   - "summary" is never dropped -- knownhost() exempts it by name;
#   - "allevents" is trimmed, never classified as a host at all;
#   - without --drop, nothing is deleted: an orphan is only reported.
#
# The case, alias and summary rows in particular exist because a fix that
# replaced knownhost() with a plain hostname lookup would silently lose them.
#
# control: passes with and without the #281 fix, and that is the point. Every
# row above is behaviour the fix must leave alone, so this file stays green with
# the fix reverted -- which for any other test would mean it proves nothing.
# Said here because a test that cannot fail is otherwise indistinguishable from
# a test that has stopped guarding its fix.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/trimhistory.sh
. "$(dirname "$0")/../lib/trimhistory.sh"

work=$(mktempdir)
setup_trimhistory "$work"

hosts_cfg "$work" \
	'127.0.0.1 Active.Example.com # conn' \
	'127.0.0.1 aliased.example.com # conn CLIENT:clientalias' \
	'127.0.0.1 plainname # conn'

seed_hosthistory "$work" active.example.com          # differs in case from hosts.cfg
seed_hosthistory "$work" ACTIVE.EXAMPLE.COM
seed_svchistory  "$work" active,example,com.conn
seed_hosthistory "$work" clientalias                 # CLIENT: alias, not the hostname
seed_svchistory  "$work" clientalias.conn
seed_hosthistory "$work" plainname                   # listed, no dots
seed_hosthistory "$work" summary                     # exempt by name
seed_hosthistory "$work" nosuchhost                  # absent, no dots
seed_hosthistory "$work" gone.example.com            # absent, dotted
seed_svchistory  "$work" gone,example,com.conn
seed_histlogs    "$work" active_example_com conn
seed_histlogs    "$work" gone_example_com conn

# Entries the scan skips before it ever asks whose host they are: a dotfile,
# and a directory (someone's backup copy of a host's logs, say). Neither is a
# history file, and neither may be deleted or reported.
: >"$work/var/hist/.hidden"
mkdir -p "$work/var/hist/adirectory"

# A status log whose name is not the 24-character stamp logtime() parses is
# left alone, and its service directory therefore stays.
: >"$work/var/histlogs/active_example_com/conn/not-a-24-char-name"

# allevents is the all-hosts event log: a file, not a host.
{
	printf 'ancient.example.com conn %d %d 3600 purple red 0\n' "$TRIM_ANCIENT" "$TRIM_ANCIENT"
	printf 'gone.example.com conn %d %d 3600 red green 0\n' "$TRIM_OLD" "$TRIM_OLD"
	printf 'active.example.com conn %d %d 3600 green red 0\n' "$TRIM_RECENT" "$TRIM_RECENT"
} >"$work/var/hist/allevents"

before=$(ls "$work/var/hist" | sort)

# ---- a dry run deletes nothing ---------------------------------------------
run_trimhistory "$work" >"$work/dry.log" 2>&1 || true
assert_equal "$before" "$(ls "$work/var/hist" | sort)" \
	"a run without --drop deleted files"
assert_contains "Orphaned" "$(cat "$work/dry.log")" \
	"a run without --drop did not even report the orphan"

# ---- the real run -----------------------------------------------------------
run_trimhistory "$work" --drop --droplogs >"$work/out.log" 2>&1 || true
log=$(cat "$work/out.log")

# Kept: every name that hosts.cfg accounts for, however it is spelled.
for f in active.example.com ACTIVE.EXAMPLE.COM active,example,com.conn \
	 clientalias clientalias.conn plainname summary allevents; do
	assert_file_exists "$work/var/hist/$f" "--drop deleted $f, which belongs to a listed host"
done

# Dropped: the names hosts.cfg does not account for, dotted or not.
for f in nosuchhost gone.example.com gone,example,com.conn; do
	[ ! -e "$work/var/hist/$f" ] \
		|| { echo "$log" >&2; fail "--drop kept $f, which is not in hosts.cfg"; }
done

# Each deletion is announced, and a dot-free name is named as a host file.
assert_contains "Orphaned host-history file nosuchhost" "$log" \
	"a dot-free orphan was not reported as a host-history file"
assert_contains "gone.example.com" "$log" \
	"a dotted orphan was deleted without being reported"

# The scan's skips.
assert_file_exists "$work/var/hist/.hidden" "--drop deleted a dotfile in the history directory"
[ -d "$work/var/hist/adirectory" ] || fail "--drop removed a directory sitting in the history directory"
assert_not_contains "adirectory" "$log" "a directory in the history directory was reported as an orphan"
assert_not_contains ".hidden" "$log" "a dotfile in the history directory was reported as an orphan"

# histlogs follow the same verdict as the history files.
[ -d "$work/var/histlogs/active_example_com" ] \
	|| fail "--droplogs removed the histlogs directory of a listed host"
[ ! -d "$work/var/histlogs/gone_example_com" ] \
	|| fail "--droplogs kept the histlogs of a host that is not in hosts.cfg"
assert_file_exists "$work/var/histlogs/active_example_com/conn/not-a-24-char-name" \
	"--droplogs deleted a status log whose name it cannot read as a timestamp"
[ ! -e "$work/var/histlogs/active_example_com/conn/$TRIM_LOGSTAMP" ] \
	|| fail "--droplogs left a pre-cutoff status log in place"

# Trimming still happens on the files that survive.
kept=$(cat "$work/var/hist/active.example.com")
assert_contains "$TRIM_RECENT" "$kept" "the post-cutoff line was trimmed out of a kept file"
assert_contains "$TRIM_OLD" "$kept" "the record spanning the cutoff was dropped from a kept file"
assert_not_contains "$TRIM_ANCIENT" "$kept" "a pre-cutoff line survived in a kept file"
assert_not_contains "ancient.example.com" "$(cat "$work/var/hist/allevents")" \
	"a pre-cutoff event survived in allevents"

# A service-history file has to be trimmed as one: its timestamp is column 7,
# where a host-history file carries it in column 2. Misclassifying a kept file
# leaves it in place but reads a weekday name as the timestamp, so everything
# but the last line goes -- existence alone would not notice.
svc=$(cat "$work/var/hist/active,example,com.conn")
assert_contains "$TRIM_RECENT" "$svc" "a kept service file lost its post-cutoff record"
assert_contains "$TRIM_OLD" "$svc" "a kept service file lost the record spanning the cutoff"
assert_not_contains "$TRIM_ANCIENT" "$svc" "a kept service file was not trimmed"

echo "OK $(basename "$0")"
