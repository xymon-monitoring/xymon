#!/usr/bin/env bash
#
# A protocols.cfg mistake should say so.
#
# An unrecognised directive was a silent no-op: the step simply vanished
# and the probe reported on a conversation it never had. That is checked
# when the file is read, not when the step executes, which is why this
# test never connects to anything.
#
# The second half matters as much as the first: the config the tree SHIPS
# must produce none of these. A validator that cries wolf gets ignored,
# and then the real warnings go unread too.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"
printf '127.0.0.1\tnobody\t# conn\n' > "$work/home/etc/hosts.cfg"

run_xymonnet() {
	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
		--timeout=2 2>&1 || :
}

# --- a file with a mistyped directive --------------------------------------
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[bad]
   exepct "220"
   expect "220"
   options banner
   port 9
CFG
out=$(run_xymonnet)

grep -qi 'unknown protocols.cfg directive' <<<"$out" || fail \
	"a mistyped directive was accepted in silence. The step is dropped and the
probe still runs, so the test reports on a conversation it never had:
$out"

# --- and the shipped file must be quiet -------------------------------------
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
out=$(run_xymonnet)
noise=$(grep -ciE 'unknown protocols.cfg directive' <<<"$out" || true)
[ "$noise" -eq 0 ] || fail \
	"the protocols.cfg this tree ships triggers $noise of its own warnings:
$(grep -iE 'unknown|before any expect' <<<"$out")"

pass "a mistyped directive is reported when the file is read, and the shipped file reports none"
