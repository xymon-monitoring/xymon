#!/usr/bin/env bash
# A protocols.cfg section without a port line is still tested, on the port the
# host's /etc/services gives its name, and the log says to add one.
#
# An upgrade keeps the sections already in protocols.cfg, and the [smtps],
# [amqp] and [amqps] shipped before 4.3.31 had no port line: they took theirs
# from /etc/services. Without the fallback they would stop being tested on an
# upgraded server; without the warning nobody would know to fix them. A port
# line still wins, and a name /etc/services does not know is still refused.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

# A name every /etc/services carries, and the port it gives it.
want=$(awk '$1 == "ftp" && $2 ~ /\/tcp$/ { split($2, a, "/"); print a[1]; exit }' \
	/etc/services 2>/dev/null || :)
[ -n "$want" ] || skip "/etc/services does not name ftp/tcp"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"
printf '127.0.0.1\thh\t# ftp nosuchsvc-xymon\n' > "$work/home/etc/hosts.cfg"

run_xymonnet() {
	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
		--timeout=2 --dump=before 2>&1 || :
}

# --- no port line: /etc/services, with a warning ----------------------------
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[ftp]
   expect "220"

[nosuchsvc-xymon]
   expect "x"
CFG
out=$(run_xymonnet)

grep -q "^Service ftp, port $want," <<<"$out" || fail \
	"an entry without a port line was not given /etc/services' port $want:
$(grep '^Service ftp' <<<"$out" || echo '(no ftp service loaded)')"

grep -q "Service ftp has no 'port' line in protocols.cfg; using $want" <<<"$out" || fail \
	"the fallback to /etc/services was silent, so nothing tells the operator
to add the port line:
$(head -5 <<<"$out")"

# A name /etc/services does not know is not given a port from anywhere.
grep -q '^Service nosuchsvc-xymon, port 0,' <<<"$out" || fail \
	"a name /etc/services does not know got a port anyway:
$(grep '^Service nosuchsvc' <<<"$out")"
grep -q "Give the nosuchsvc-xymon service a 'port' line" <<<"$out" || fail \
	"an entry with no port from any source is not reported:
$(head -5 <<<"$out")"

# --- a port line wins, and nothing is said ---------------------------------
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[ftp]
   expect "220"
   port 2121
CFG
out=$(run_xymonnet)

grep -q '^Service ftp, port 2121,' <<<"$out" || fail \
	"/etc/services overrode the entry's own port line:
$(grep '^Service ftp' <<<"$out")"
! grep -q "has no 'port' line" <<<"$out" || fail \
	"an entry with a port line was warned about:
$(grep "has no 'port' line" <<<"$out")"

pass "an entry without a port line takes /etc/services' port ($want for ftp) and warns; a port line wins; an unknown name is still refused"
