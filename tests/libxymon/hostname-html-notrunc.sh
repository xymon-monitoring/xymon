#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/hostname-html-notrunc.sh
#
# hostnamehtml() (lib/htmllog.c) wraps a host's display name and COMMENT in an
# <A ...><FONT>...</FONT></A>. It used a fixed 4096-byte buffer, so a long name
# or comment was cut -- dropping the closing tags and possibly splitting a
# UTF-8 character. The buffer now grows to fit. A display name past 4096 bytes
# must come back whole, closing tags and all.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-harness.sh
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
lib=libxymon.a
[ -f "$(xymon_lib "$ROOT" "$lib")" ] || lib=libxymonclient.a
[ -f "$(xymon_lib "$ROOT" "$lib")" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" "$lib" libxymoncomm.a
HARNESS_CFLAGS="-iquote $ROOT/include -iquote $ROOT/lib" \
	build_harness "$work" "$work/harness" tests/libxymon/hostname-html-notrunc-harness.c "$lib" libxymoncomm.a

# A display name well past the old 4096-byte buffer.
dn=$(awk 'BEGIN { for (i = 0; i < 5000; i++) printf "D" }')
cat > "$work/hosts.cfg" <<CFG
127.0.0.1 longname.example.com # NAME:$dn
CFG

out=$("$work/harness" "$work/hosts.cfg" longname.example.com)

# The whole name survives (5000 D's present) and the closing tags are intact.
case "$out" in
	*"$dn"*"</FONT>"*) : ;;
	*) fail "a 5000-byte display name was cut: output is ${#out} bytes, ends '...${out: -20}'" ;;
esac
[ "${#out}" -gt 4096 ] || fail "output is only ${#out} bytes; the long name was truncated"

pass "hostnamehtml grows its buffer to fit a display name past 4096 bytes"
