#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/http-forced-ip-label.sh
#
# An http or sslcert test may force the target address with "=IP" in the spec
# (https://example.com=1.2.3.4/). decode_url() strips that address out of the
# canonical URL -- so two tests on one URL forcing different addresses used to
# share a status label, a history and an RRD file (issue #958). urlwithforcedip()
# puts the forced address back into the label, after the host, and only when it
# was forced (desturl->ipforced), so the per-address labels and RRD files follow
# while a plain test's label stays byte-for-byte what it was.
#
# The harness drives the real decode_url() + urlwithforcedip() and checks the
# contract: "=IP" reappears exactly once after the host, the credentials
# decode_url() removed do not, and a non-forced spec is returned unchanged.
#
# Filed under tests/xymonnet, not tests/libxymon: url.o (decode_url,
# urlwithforcedip) is in the server libraries, so a client-only build has
# nothing to link.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

[ -f "$ROOT/lib/url.c" ] || skip "lib/url.c not present in this checkout"

require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")
claim="urlwithforcedip() shows a forced =IP in the label, drops credentials, leaves a plain URL unchanged"

# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" -g $cflags -o "$work/harness" \
	"$here/http-forced-ip-label-harness.c" \
	"$(xymon_lib "$ROOT" libxymoncomm.a)" $ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "http-forced-ip-label harness does not build"; }

if "$work/harness" >"$work/out" 2>&1; then
	pass "$claim"
else
	sed -n '1,40p' "$work/out" >&2
	fail "$claim"
fi
