#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymongrep-filter.sh
#
# Behavioural test of the BUILT xymongrep binary: hosts.cfg tag selection.
#
# This is the suite's first test that drives a compiled tool -- the
# require_bin consumer that tests/lib/assert.sh and the post-build suite run
# in .github/workflows/build.yml were staged for. It runs in three
# environments:
#   - build.yml (server leg): common/xymongrep was just built; the in-tree
#     default below finds it;
#   - tests.yml (no build): require_bin skips with 77;
#   - Debian autopkgtest: the control file exports
#     XYMONGREP=/usr/lib/xymon/client/bin/xymongrep (or the server-package
#     path) and the test exercises the INSTALLED binary. Unlike the
#     source-reading tests in this suite, running the shipped binary is what
#     gives library-transition CI (libpcre2, OpenSSL, ...) something that can
#     actually break at runtime.
#
# What it asserts is xymongrep's documented selection contract
# (common/xymongrep.1, stable since the bbhostgrep days) -- easy to break in
# a hosts.cfg-parser or tag-walk rework, and exercised through the real
# load_hostnames()/xmh_item() path in libxymon:
#   - a bare test name matches a tag exactly (case-insensitive), so `http`
#     does NOT select `http://...` URL tags;
#   - a trailing `*` prefix-matches, so `http*` is how URL tags are selected;
#   - only the matched tags are echoed back, not the host's full tag list;
#   - a matched dialup host gets the `dialup` flag appended, dropped by
#     --noextras (the behaviour PR #94's --no-dialup will key on; that PR
#     extends this file once merged).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

# Server builds produce common/xymongrep; client-only builds ship the same
# tool as client/xymongrep. Probe the server path first, fall back to the
# client one; an explicit $XYMONGREP (CMake out-of-source, autopkgtest) is
# used verbatim by require_bin.
default="common/xymongrep"
if [ -z "${XYMONGREP:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymongrep" ]; then
	default="client/xymongrep"
fi
require_bin XYMONGREP "$default"

work=$(mktempdir)
cat >"$work/hosts.cfg" <<'EOF'
1.2.3.4		www.example.com		# http://www.example.com conn
5.6.7.8		db.example.com		# conn pop3
9.9.9.9		dial.example.com	# dialup http://dial.example.com
0.0.0.0		notags.example.com	#
EOF

# --hosts= keeps the test hermetic: it bypasses both the $HOSTSCFG fallback
# and the try-xymond-first path of load_hostnames(), so no server is needed.

# Exact tag match: `conn` selects the two hosts tagged conn and echoes only
# the matched tag.
out=$("$XYMONGREP" --hosts="$work/hosts.cfg" conn)
assert_contains "www.example.com # conn" "$out" "exact match must select www.example.com"
assert_contains "db.example.com # conn" "$out" "exact match must select db.example.com"
assert_not_contains "pop3" "$out" \
	"only the matched tag is echoed, not the host's full tag list"
assert_not_contains "dial.example.com" "$out" "hosts without the tag must not appear"
assert_not_contains "notags.example.com" "$out" "hosts with no tags must not appear"

# Exact means exact: a bare `http` must not prefix-match URL tags.
out=$("$XYMONGREP" --hosts="$work/hosts.cfg" http)
[ -z "$out" ] || fail "bare 'http' must not match http:// URL tags (got: $out)"

# Trailing-* prefix match is how URL tags are selected; the matched dialup
# host carries the appended `dialup` flag (extras are on by default).
out=$("$XYMONGREP" --hosts="$work/hosts.cfg" 'http*')
assert_contains "www.example.com # http://www.example.com" "$out" \
	"http* must select the URL tag"
assert_contains "dial.example.com # http://dial.example.com dialup" "$out" \
	"matched dialup host must carry the appended dialup flag"
assert_not_contains "db.example.com" "$out" \
	"http* must not select hosts without an http tag"

# --noextras drops the appended flags, leaving only the matched tags.
out=$("$XYMONGREP" --noextras --hosts="$work/hosts.cfg" 'http*')
assert_not_contains "dialup" "$out" "--noextras must drop the dialup flag"

pass "xymongrep tag selection contract holds (exact, prefix-*, echoed tags, dialup flag)"
