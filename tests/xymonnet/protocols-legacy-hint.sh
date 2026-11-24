#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/protocols-legacy-hint.sh
#
# options ssl, telnet and banner still work, but a step says the same thing and
# says where it happens: "start tls" as the first step, "start iac", or an
# "expect". The shipped entries use the step -- protocols-options.sh keeps it
# that way -- so a file that still carries one of the three options is a custom
# one, and reading it logs a hint naming the step to use instead.
#
# The hint is advisory, not a refusal (an unknown option refuses the entry; see
# dialogue-validation.sh). The three options keep working, which their own tests
# cover; here we check only that each logs its replacement when read.
#
# Driven by the real xymonnet reading a protocols.cfg that uses each. Dropping
# an errprintf fails its row.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"
printf '127.0.0.1\tnobody\t# conn\n' > "$work/home/etc/hosts.cfg"

cat > "$work/home/etc/protocols.cfg" <<'CFG'
[legssl]
   options ssl
   port 9
[legtelnet]
   options telnet
   port 9
[legbanner]
   options banner
   port 9
CFG

out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip --timeout=2 2>&1 || :)

# hint NAME OPTION STEP -- the entry NAME, using "options OPTION", is told to
# prefer STEP, and its hint says both forms work.
hint() {
	line=$(grep -iF "Service $1: 'options $2'" <<<"$out" || :)
	grep -qiF "Service $1: 'options $2' is the older spelling" <<<"$line" \
		|| fail "an entry with 'options $2' logged no hint:
$out"
	grep -qiF "$3" <<<"$line" \
		|| fail "the '$2' hint did not name '$3' as the step to use:
$line"
}

hint legssl    ssl    "start tls"
hint legtelnet telnet "start iac"
hint legbanner banner "expect"

# Advisory, not a refusal: the hint must not read as the entry being rejected.
sslline=$(grep -iF "Service legssl: 'options ssl'" <<<"$out" || :)
grep -qiE 'unknown|refus|dropped' <<<"$sslline" \
	&& fail "the ssl hint reads as a refusal; the entry still runs" || :

pass "options ssl, telnet and banner each log a hint naming the step to use instead, without refusing the entry"
