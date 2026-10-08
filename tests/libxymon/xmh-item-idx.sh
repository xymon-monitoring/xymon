#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/xmh-item-idx.sh
#
# xmh_item_idx() must recognize every reserved hosts.cfg tag (#278). Its two
# callers act on the answer: the info page lists an unrecognized tag again
# under "Other tags", and xymonnet takes it for a network test and splits it
# in place inside the host record.
#
# It stopped at the first slot without a key. The computed items (XMH_IP,
# XMH_HOSTNAME, ...) have none, and ten tag keys were added after them --
# CLASS:, OS:, DOC:, NOPROP:, COMPACT: and five more -- so it never saw those.
#
# The keys are read from lib/loadhosts.c rather than listed here, so a key
# added to the table later is covered without touching this test. The answer
# comes from the real library, through xmh-item-idx-harness.c.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

LOADHOSTS_C="$ROOT/lib/loadhosts.c"
[ -f "$LOADHOSTS_C" ] || skip "lib/loadhosts.c not present in this checkout"

require_c_buildenv "$ROOT"
[ -f "$ROOT/lib/libxymonclient.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymonclientcomm.a libxymonclient.a

keys=$(grep -o 'xmh_item_key\[XMH_[A-Z0-9_]*\][[:space:]]*=[[:space:]]*"[^"]*"' "$LOADHOSTS_C" \
	| sed -E 's/.*"([^"]*)"$/\1/')
[ -n "$keys" ] || fail "no xmh_item_key[] entries found in lib/loadhosts.c -- the table moved"

# A key ending in ':' or '=' takes a value; a flag is the whole tag.
{
	while IFS= read -r key; do
		case $key in
			*:|*=) printf '+ %sx\n' "$key" ;;
			*) printf '+ %s\n' "$key" ;;
		esac
	done <<<"$keys"
	# Controls: test specs xymonnet must keep treating as tests.
	printf -- '- %s\n' conn ssh 'ssh:22' '!ssh' '?conn' '~ftp' \
		'http://www.example.com/' 'https://www.example.com/' \
		'cont=http://www.example.com/;ok' 'dns=www.example.com' 'ntp' 'notareservedtag'
} > "$work/cases"

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086 # the flag strings are lists of words
"$CC" $harness_cflags -o "$work/harness" \
	"$here/xmh-item-idx-harness.c" \
	"$ROOT/lib/libxymonclientcomm.a" "$ROOT/lib/libxymonclient.a" \
	$harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" "$work/cases" 2>"$work/stderr.log" \
	|| fail "xmh_item_idx() misreads hosts.cfg tags:
$(cat "$work/stderr.log")"

pass "xmh_item_idx() recognizes every reserved hosts.cfg tag and no test spec"
