#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/config-reply-empty-dir.sh
#
# xymond's reply to "config hosts.cfg" must carry every line of the file, even
# past an empty 'optional directory' include.
#
# Every shipped config ends with such an include (#222), and the directory is
# empty until someone drops a fragment into it. stackfgets() (lib/stackio.c)
# answers an empty directory with a blank line, but it blanked only the string:
# it put a NUL in the line's first byte, left the buffer's length as it was,
# and had already turned the newline into a NUL. get_config() (xymond.c) copies
# each line by length, so the reply carried the directive line with two NULs in
# it -- and load_hostnames() reads the reply as a C string, so every program
# that loads hosts.cfg through xymond lost every host after the directive:
# xymond_alert sent no alerts for them, xymond_client analysed none of their
# client data. stackfgets() now empties the buffer itself.
#
# An include whose file cannot be opened -- an 'optional include' of a file
# not there yet -- lost the hosts after it the same way when its line had
# trailing whitespace: stackfgets() blanked that whitespace with NULs to end
# the file name, and returned the line with them still inside its length. It
# now ends the name without blanking, and returns the line as written.
#
# Driven through the real xymond, because the reply is what those programs
# read. A directory holding a fragment takes another branch of stackfgets(),
# and is checked too, as the contrast.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon

work=$(mktempdir)

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	local i=0
	while kill -0 "$XYMOND_PID" 2>/dev/null && [ "$i" -lt 100 ]; do
		sleep 0.1
		i=$((i+1))
	done
	XYMOND_PID=""
}
register_cleanup stop_xymond

# get_config() serves $XYMONHOME/etc/<name>, and it must be a regular file.
mkdir -p "$work/home/etc/hosts.d" "$work/home/tmp" "$work/home/www"
cat >"$work/home/etc/hosts.cfg" <<EOF
127.0.0.1 beforehost # noconn
optional directory $work/home/etc/hosts.d
127.0.0.2 afterhost # noconn
EOF

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/home/etc/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond

# fetch FILE -- xymond's reply to "config hosts.cfg", byte for byte. Kept in a
# file: a shell variable cannot hold a NUL, which is the thing to look for.
fetch() {
	"$XYMONCLIENT" "127.0.0.1:$PORT" "config hosts.cfg" > "$1" \
		|| fail "xymond did not answer 'config hosts.cfg': $(cat "$work/xymond.log")"
}

# nul_count FILE -- how many NUL bytes FILE holds.
nul_count() {
	local all without
	all=$(wc -c < "$1")
	without=$(tr -d '\000' < "$1" | wc -c)
	echo $(( all - without ))
}

# Empty directory: the line after the directive must survive, with no NUL.
fetch "$work/reply-empty"
assert_contains "beforehost" "$(tr -d '\000' < "$work/reply-empty")" \
	"the config reply lost the line before the directive"
[ "$(nul_count "$work/reply-empty")" -eq 0 ] \
	|| fail "the config reply carries $(nul_count "$work/reply-empty") NUL byte(s) where an empty 'optional directory' was -- a reader stops there"
assert_contains "afterhost" "$(cat "$work/reply-empty")" \
	"the config reply lost the line after an empty 'optional directory'"

# Contrast: a directory holding a fragment merges it, and keeps the rest.
printf '127.0.0.3 fragmenthost # noconn\n' > "$work/home/etc/hosts.d/10-extra.cfg"
fetch "$work/reply-fragment"
[ "$(nul_count "$work/reply-fragment")" -eq 0 ] \
	|| fail "the config reply carries a NUL byte with a fragment in the directory"
assert_contains "fragmenthost" "$(cat "$work/reply-fragment")" \
	"the config reply did not merge the fragment from the directory"
assert_contains "afterhost" "$(cat "$work/reply-fragment")" \
	"the config reply lost the line after a non-empty 'optional directory'"

# A failed include, trailing whitespace and all, must come back as written.
printf '127.0.0.1 beforehost # noconn\noptional include %s/missing.cfg \t \n127.0.0.2 afterhost # noconn\n' \
	"$work" > "$work/home/etc/hosts.cfg"
fetch "$work/reply-missing"
[ "$(nul_count "$work/reply-missing")" -eq 0 ] \
	|| fail "the config reply carries $(nul_count "$work/reply-missing") NUL byte(s) where an 'optional include' of a missing file had trailing whitespace -- a reader stops there"
cmp -s "$work/reply-missing" "$work/home/etc/hosts.cfg" \
	|| fail "the config reply does not return hosts.cfg as written when an 'optional include' names a missing file"

pass "xymond's config reply keeps every line past an 'optional directory', empty or not, and past an include it cannot open, with no NUL"
