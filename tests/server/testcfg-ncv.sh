#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/testcfg-ncv.sh
#
# test.cfg's per-metric NCV/SPLITNCV spec overrides the NCV_<col>/
# SPLITNCV_<col> environment for the RRD writer. Feed a status message to
# the built xymond_rrd with the spec supplied ONLY via test.cfg (no env)
# and assert the datasets it produces; then confirm SPLITNCV makes one file
# per variable. Uses rrdtool to inspect the created files.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"
work=$(mktempdir)

feed() {  # feed <testname> <bodyfile> [ENV=val...]
	local ts; ts=$(date +%s); local tn="$1" body="$2"; shift 2
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|%s|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" "$tn" $((ts+1800)) "$ts" "$ts"
		cat "$body"
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" "$@" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

mkdir -p "$work/etc"
cat >"$work/etc/test.cfg" <<'EOF'
TEST appstat {
        SOURCE client
        METRIC appstat { NCV queries:DERIVE, latency:GAUGE }
}
TEST splitstat {
        SOURCE client
        METRIC splitstat { SPLITNCV *:GAUGE }
}
EOF

cat >"$work/body" <<'EOF'
App statistics
queries : 100
latency : 5
EOF

# NCV spec comes ONLY from test.cfg (no NCV_appstat env): plain NCV makes a
# single appstat.rrd holding the named datasets.
out=$(feed appstat "$work/body")
assert_contains "appstat.rrd" "$out" "test.cfg NCV spec drives creation without the env"

# SPLITNCV from test.cfg: one file PER VARIABLE. This is the rrdtool-free
# proof the spec was honoured - without it (no env either) do_ncv would fall
# through to non-split and make a single splitstat.rrd, not split files.
cat >"$work/body2" <<'EOF'
Split statistics
alpha : 1
beta : 2
EOF
out=$(feed splitstat "$work/body2")
assert_contains "splitstat,alpha.rrd" "$out" "test.cfg SPLITNCV makes one file per variable"
assert_contains "splitstat,beta.rrd" "$out" "test.cfg SPLITNCV makes one file per variable"
assert_not_contains "splitstat.rrd" "$out" "SPLITNCV did not collapse into one file"

# When an rrdtool CLI is present, also verify the NCV dataset TYPES came from
# test.cfg (queries as DERIVE, not the default GAUGE).
if command -v rrdtool >/dev/null 2>&1; then
	feed appstat "$work/body" >/dev/null
	t=$(rrdtool info "$work/rrd/testhost/appstat.rrd" 2>/dev/null | sed -n 's/^ds\[queries\].type = "\([^"]*\)"/\1/p')
	[ "$t" = "DERIVE" ] || fail "test.cfg NCV type not applied: queries is '$t', wanted DERIVE"
fi

# The must-not-change requirement: the env path is untouched when test.cfg
# does not mention the column (envstat below) - and when test.cfg is absent
# entirely. Both must store via NCV_<col> exactly as upstream does.
cat >"$work/body3" <<'EOF'
Env statistics
hits : 42
EOF
out=$(feed envstat "$work/body3" TEST2RRD="envstat=ncv" NCV_envstat="hits:GAUGE")
assert_contains "envstat.rrd" "$out" "column absent from test.cfg falls back to NCV_<col> env"
mv "$work/etc/test.cfg" "$work/etc/test.cfg.away"
out=$(feed envstat "$work/body3" TEST2RRD="envstat=ncv" NCV_envstat="hits:GAUGE")
assert_contains "envstat.rrd" "$out" "no test.cfg at all falls back to NCV_<col> env"
mv "$work/etc/test.cfg.away" "$work/etc/test.cfg"

pass "test.cfg NCV/SPLITNCV specs override the environment for the RRD writer"
