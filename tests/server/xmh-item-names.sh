#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xmh-item-names.sh
#
# Guards the XMH_ item-name table in lib/loadhosts.c. Those names are public:
# clients select and filter xymondboard/hostinfo fields by them, xymond emits
# them in hostinfo responses (which lib/loadhosts_net.c parses back), web
# templates resolve them via xmh_item_byname(), and xymon-xmh(5) documents
# them.
#
# XMH_FLAG_MULTIHOMED was registered as "XMH_MULTIHOMED", missing the FLAG_
# that both its enum name and xymon-xmh(5) carry, so the documented name did
# not resolve and MULTIHOMED was never treated as a flag.
#
# Two general guards back up the MULTIHOMED-specific assertions:
#   - a source check that each xmh_item_name[XMH_X] entry registers the string
#     "XMH_X" -- this is what actually catches the typo class, since a
#     misnamed entry is still internally self-consistent (the wrong name
#     resolves back to its own slot perfectly well) and no runtime assertion
#     can see the mismatch;
#   - a runtime check that every registered name resolves back to its own
#     entry, which catches duplicates and collisions instead.
#
# See xmh-item-names-harness.c for details. The runtime half is driven
# entirely through the real library's public interface.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

LOADHOSTS_C="$ROOT/lib/loadhosts.c"
[ -f "$LOADHOSTS_C" ] || skip "lib/loadhosts.c not present in this checkout"

# --- Source check: xmh_item_name[XMH_X] must register the string "XMH_X".
# A mismatch is invisible at runtime (the wrong name still resolves to its own
# slot), so this has to be checked against the source.
mismatched=$(grep -o 'xmh_item_name\[XMH_[A-Z0-9_]*\][[:space:]]*=[[:space:]]*"[^"]*"' "$LOADHOSTS_C" \
	| sed -E 's/xmh_item_name\[(XMH_[A-Z0-9_]*)\][[:space:]]*=[[:space:]]*"([^"]*)"/\1 \2/' \
	| awk '$1 != $2 { printf "  %s registered as \"%s\"\n", $1, $2 }')
[ -z "$mismatched" ] || fail "xmh_item_name[] entries disagree with their enum names -- \
the registered string is the public field name (xymon-xmh(5), xymondboard/hostinfo, web \
templates) and also what xmh_item_isflag[] is derived from:
$mismatched"

require_c_buildenv "$ROOT"
[ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"


work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-xmh-item-names.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cat > "$work/hosts.cfg" <<'EOF'
127.0.0.1 taghost.example.com # conn dialup MULTIHOMED
127.0.0.1 plainhost.example.com # conn
EOF

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/harness" \
	"$here/xmh-item-names-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" "$work/hosts.cfg" 2>"$work/stderr.log" \
	|| fail "XMH_ item-name assertions failed:
$(cat "$work/stderr.log")"

pass "every XMH_ item name resolves to its own entry and MULTIHOMED behaves as a flag"
