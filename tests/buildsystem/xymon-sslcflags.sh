#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/xymon-sslcflags.sh
#
# xymon_sslcflags() hands a harness the tree's SSLFLAGS and SSLINCDIR, and
# nothing of its CFLAGS.
#
# The build adds those two only to the compile lines of the files that use
# OpenSSL. A harness that includes <openssl/ssl.h> needs them where OpenSSL is
# outside the compiler's default path, as Homebrew's openssl@3 is on macOS:
# without them, every TLS harness failed to compile there and its test
# skipped.
#
# The helper asks make, so the fixtures are trees with a minimal Makefile
# whose variables stand for what configure writes.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_gnu_make

work=$(mktempdir)

mkdir -p "$work/macos" "$work/nossl" "$work/unconfigured"
printf '%s\n' 'CFLAGS = -O2 -DXYMON_CFLAGS_SENTINEL' \
	'SSLFLAGS = -DHAVE_OPENSSL' \
	'SSLINCDIR = -I/opt/homebrew/opt/openssl@3/include' > "$work/macos/Makefile"
printf '%s\n' 'CFLAGS = -O2' > "$work/nossl/Makefile"

macos=$(xymon_sslcflags "$work/macos")
nossl=$(xymon_sslcflags "$work/nossl")
unconfigured=$(xymon_sslcflags "$work/unconfigured")

assert_contains "-I/opt/homebrew/opt/openssl@3/include" "$macos" \
	"a tree's SSLINCDIR is missing from the harness flags, so <openssl/ssl.h> is not found where OpenSSL is outside the default path"
assert_contains "-DHAVE_OPENSSL" "$macos" \
	"a tree's SSLFLAGS are missing from the harness flags"
case $macos in *XYMON_CFLAGS_SENTINEL*|*-O2*)
	fail "xymon_sslcflags passed on the tree's CFLAGS, which xymon_cflags already covers: $macos" ;;
esac
[ -z "$(printf '%s' "$nossl" | tr -d ' ')" ] \
	|| fail "a tree configured without OpenSSL variables gave harness flags anyway: '$nossl'"
[ -z "$unconfigured" ] || fail "an unconfigured tree gave harness flags: '$unconfigured'"

pass "xymon_sslcflags carries SSLFLAGS and SSLINCDIR, and none of CFLAGS"
