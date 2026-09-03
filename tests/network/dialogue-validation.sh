#!/usr/bin/env bash
# A protocols.cfg mistake should say so, and stop the entry.
#
# An unresolvable line used to be a silent no-op: the step vanished and the
# probe reported on a conversation it never had. "start tsl" was logged and
# the probe ran anyway, GREEN, while nothing had been upgraded.
#
# So the colour is what is checked here, not the log -- yellow, because the
# check is broken rather than the service. And the shipped file must trigger
# none of it: a validator that cries wolf gets ignored.

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

# --- a "start" whose feature does not exist ---------------------------------
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[badfeat]
   expect "220"
   start tsl
   options banner
   port 9
CFG
out=$(run_xymonnet)
grep -qi "a client can start 'tls' or 'iac'" <<<"$out" || fail \
	"'start tsl' was accepted. The word after start names code in xymonnet, so
a typo cannot be resolved later -- it has to be caught when the file is read:
$out"

# --- "options ssl" and "start tls" together ----------------------------------
#
# One connection is encrypted once. With ssl the handshake has already
# happened at connect, so the upgrade step silently does nothing -- a file
# that says it upgrades never does, and the operator cannot tell from the
# column that the plaintext half it describes was never spoken.
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[bothtls]
   expect "220"
   start tls
   options ssl
   port 9
CFG
out=$(run_xymonnet)
grep -qiE "both 'options ssl' and 'start tls'" <<<"$out" || fail \
	"an entry asking for TLS twice was accepted. 'options ssl' negotiates at
connect and 'start tls' upgrades mid-session; asking for both leaves the
upgrade a no-op, so the conversation the file describes never happens:
$out"

# There is no "connect-only" option to contradict the steps any more. It never
# reached the probe -- an entry with no steps already checks only that the port
# opens -- and the sole rule it carried was that it could not sit beside a step,
# which was a contradiction only because the keyword existed. Which entries are
# deliberately stepless is pinned in protocols-stepless.sh instead.

# --- a mistyped "until" ------------------------------------------------------
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[baduntil]
   expect "220" untill "220 "
   options banner
   port 9
CFG
out=$(run_xymonnet)
grep -qi "expect takes only 'until'" <<<"$out" || fail \
	"'untill' was accepted. The terminator is silently dropped with it, so the
expect takes a single line, matches the first of a multi-line reply, and
leaves the rest for the next step:
$out"

# --- and the shipped file must be quiet -------------------------------------
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
out=$(run_xymonnet)
noise=$(grep -ciE 'unknown protocols.cfg directive|unknown option in service|expect takes only|says nothing is sent' <<<"$out" || true)
[ "$noise" -eq 0 ] || fail \
	"the protocols.cfg this tree ships triggers $noise of its own warnings:
$(grep -iE 'unknown|before any expect' <<<"$out")"

# --- a refusal is not hidden by a port that is closed ------------------------
#
# Everything below points at a live peer. When the port is shut, the
# unavailable-socket path runs first and reports "Service unavailable", which
# is true of the connection and says nothing about the definition -- the
# operator is sent to look at a service when the file is what is broken, and
# the refusal the manual promises is nowhere in the status.
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[deadref]
   expect "220"
   start tsl
   port 9
CFG
printf '127.0.0.1\tdeadhost\t# deadref\n' > "$work/home/etc/hosts.cfg"
out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=5 2>&1 || :)
colour=$(grep -oE 'deadhost\.deadref (green|yellow|red|clear)' <<<"$out" | awk '{print $2}' | head -1)

[ "$colour" = "yellow" ] || fail \
	"a refused entry on a closed port reported '$colour'. The connection failing
is true but not the point: the definition was rejected when the file was read,
so no check ran, and the status names a service fault instead of the config
one that has to be fixed first:
$(grep -iE 'deadref' <<<"$out" | head -3)"

grep -qi 'refused this definition' <<<"$out" || fail \
	"the status for a refused entry on a closed port carries no reason:
$(grep -i deadref <<<"$out" | head -3)"

# --- a refused entry must not report the service up -------------------------
#
# Everything above checks that a complaint is printed. That was the whole of
# this test, and it is what let the bug through: the complaint was printed,
# the step was dropped, and the probe went green anyway. So run one against a
# server that answers, and look at what the operator would see.
#
# --checkresponse is how the shipped tasks.cfg runs xymonnet; without it no
# expect is consulted at all and this would prove nothing.
: "${CC:=cc}"
# Without a compiler the live-peer half cannot run. Say so in the result
# rather than reporting the full pass below: every colour assertion -- the
# ones that caught a refused entry going green -- lives inside this block.
command -v "$CC" >/dev/null 2>&1 || skip \
	"no C compiler: every colour assertion below needs a live peer, and a PASS
here would be read as having checked them"

if command -v "$CC" >/dev/null 2>&1; then
	"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" || \
		skip "dialogue-peer does not compile against libssl"

	printf '%s\n' 'send "220 test.local ESMTP\r\n"' \
		      'recv ehlo' \
		      'send "250 ok\r\n"' \
		      'hold 5' > "$work/peer.script"

	# dialogue-peer serves ONE connection and exits, so each case gets its
	# own. Reusing a spent peer produces "Connection refused", which is not
	# green either -- an assertion could pass while testing nothing.
	: > "$work/peerpids"
	new_peer() {
		"$work/peer" "$work/peer.script" "$work/observed.$1" > "$work/port.$1" &
		echo $! >> "$work/peerpids"
		i=0; while [ "$i" -lt 60 ]; do [ -s "$work/port.$1" ] && break; sleep 0.1; i=$((i + 1)); done
		cat "$work/port.$1"
	}
	register_cleanup "kill \$(tr '\n' ' ' < '$work/peerpids') 2>/dev/null || :"

	port=$(new_peer 1)
	[ -n "$port" ] || skip "the test peer never named a port"

	printf '[badfeat]\n   expect "220"\n   send "ehlo xymonnet\\r\\n"\n   expect "250"\n   start tsl\n   options banner\n   port %s\n' \
		"$port" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\thost1\t# badfeat\n' > "$work/home/etc/hosts.cfg"

	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 2>&1 || :)
	colour=$(grep -oE 'status\+[0-9]+ host1\.badfeat (green|yellow|red|clear)' <<<"$out" | awk '{print $3}' | head -1)

	[ "$colour" != "green" ] || fail \
		"a refused entry reported the service UP. The server answered, so every
step that was READ succeeded -- but the one that was refused simply vanished,
and the column says the probe checked something it never did:
$(grep -iE 'badfeat|can start' <<<"$out" | head -4)"

	[ "$colour" = "yellow" ] || fail \
		"a refused entry reported '$colour'. It should be yellow by default: the
check is broken, not the service, and a config typo should not page anyone --
but it must not be invisible either. (--checkresponse=red escalates it.)
$(grep -iE 'badfeat' <<<"$out" | head -3)"

	grep -qi 'refused this definition' <<<"$out" || fail \
		"the status carries no reason. An operator seeing a yellow column has to
be told the definition was refused, or the log is the only place to find out:
$(grep -i badfeat <<<"$out" | head -3)"

	# --- 2. a mistyped directive must not report the service up either ------
	#
	# The line is not understood, the step never exists, and what runs is a bare
	# greeting check: the server answers "220", nothing is sent, and the column
	# says the service is fine.
	port=$(new_peer 2)
	printf '[badword]\n   expect "220"\n   sedn "ehlo xymonnet\\r\\n"\n   exepct "250"\n   options banner\n   port %s\n' \
		"$port" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\thost2\t# badword\n' > "$work/home/etc/hosts.cfg"

	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 2>&1 || :)
	colour=$(grep -oE 'status\+[0-9]+ host2\.badword (green|yellow|red|clear)' <<<"$out" | awk '{print $3}' | head -1)

	[ "$colour" != "green" ] || fail \
		"an entry with two mistyped directives reported the service UP. Both
steps were dropped, so what ran was a bare greeting check -- the probe never
sent anything, and the column claims a conversation that did not happen:
$(grep -iE 'badword|unknown' <<<"$out" | head -4)"

	# --- 3. a second "start tls" is not a second upgrade --------------------
	#
	# There is one connection and it is upgraded once. A file asking twice asks
	# for something that cannot happen, and the second was skipped in silence.
	port=$(new_peer 3)
	printf '[twice]\n   expect "220"\n   send "ehlo xymonnet\\r\\n"\n   expect "250"\n   start tls\n   start tls\n   options banner\n   port %s\n' \
		"$port" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\thost3\t# twice\n' > "$work/home/etc/hosts.cfg"

	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 2>&1 || :)
	colour=$(grep -oE 'status\+[0-9]+ host3\.twice (green|yellow|red|clear)' <<<"$out" | awk '{print $3}' | head -1)
	[ "$colour" != "green" ] || fail \
		"an entry asking to upgrade twice reported the service UP:
$(grep -iE 'twice' <<<"$out" | head -3)"
	grep -qi "second 'start tls'" <<<"$out" || fail \
		"a second 'start tls' was accepted in silence. One connection is upgraded
once; a file that asks twice is asking for something that cannot happen, and
it should be told when the file is read rather than have the line ignored:
$(grep -iE 'twice|start' <<<"$out" | head -4)"

	# --- a protocols.cfg that cannot be read is not a licence to guess -----------
	#
	# xymonnet once carried a second set of probes compiled into
	# lib/netservices.c and used them when the file was missing. They were older
	# and weaker -- sending before the greeting, matching the greeting rather
	# than the reply -- so the columns kept reporting while the checks behind
	# them had quietly changed.
	#
	# The file is the configuration: if it cannot be read, nothing was asked for,
	# and the same rule applies as to a single unreadable entry. Nothing is
	# compiled in any more -- find_tcp_service() hands back a refused definition
	# for whatever is asked of it, so there is something to report against.
	#
	# A live peer, or a refused connection would answer for us and this would
	# pass without the missing-file path ever running. "svc:port" is how
	# hosts.cfg points a service at another port.
	rm -f "$work/home/etc/protocols.cfg"
	printf '%s\n' 'send "220 FTP ready\r\n"' 'recvany' 'hangup' > "$work/peer.script"
	nofileport=$(new_peer nofile)
	[ -n "$nofileport" ] || skip "the peer for the missing-file case never named a port"

	printf '127.0.0.1\tnofile\t# ftp:%s\n' "$nofileport" > "$work/home/etc/hosts.cfg"
	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=5 2>&1 || :)
	colour=$(grep -oE 'nofile\.ftp (green|yellow|red|clear)' <<<"$out" | awk '{print $2}' | head -1)

	[ "$colour" != "green" ] || fail \
		"with no protocols.cfg at all, the ftp probe reported the service up. The
	checks that ran were not the ones this tree ships -- they came from the
	entries this tree ships:
	$(grep -i nofile <<<"$out" | head -3)"

	# The STATUS line, not the log: the operator reading a yellow column has
	# to be told there why, and "Unexpected service response" on its own says
	# nothing about the file being unreadable.
	# And it must not have spoken. A refused definition sends nothing -- the
	# compiled-in entries are refused too, and the driver has to honour that.
	i=0; while [ "$i" -lt 50 ] && ! grep -q '^done' "$work/observed.nofile" 2>/dev/null; do
		sleep 0.1; i=$((i + 1))
	done
	! grep -q '^got ' "$work/observed.nofile" 2>/dev/null || fail \
		"with no protocols.cfg the probe still sent its command. The entry is
	refused, so nothing should reach the wire:
	$(cat "$work/observed.nofile")"

	grep -E 'Service ftp on nofile' <<<"$out" | grep -qi 'could not be read' || fail \
		"the status for a service checked with no protocols.cfg carries no
	reason -- only the log says the file could not be read:
	$(grep -i nofile <<<"$out" | head -3)"

	# --- 4. a refused entry must not speak to the server --------------------
	#
	# Refusing a definition and refusing to RUN it are different things. The
	# refusal is recorded on the entry, but a classic "send then expect" is not
	# a dialogue, so the legacy path handled it and wrote the command out. On an
	# entry that logs in, that is a rejected config sending credentials.
	printf '%s\n' 'recvany' 'send "250 ok\r\n"' 'hangup' > "$work/peer.script"
	port=$(new_peer 4)
	printf '[mute]\n   send "ehlo xymonnet\\r\\n"\n   expect "250"\n   options bogusopt\n   port %s\n' \
		"$port" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\thost4\t# mute\n' > "$work/home/etc/hosts.cfg"

	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 2>&1 || :)

	# The peer records every line it reads, and "eof" when the far end closes
	# without sending one. Insisting the file EXISTS and is non-empty is what
	# makes the check mean something: if the probe never connected there
	# would be nothing to grep, and an absent file would satisfy any test
	# phrased only as "no command was sent".
	# The peer flushes on exit; give it a moment to get there.
	i=0; while [ "$i" -lt 50 ] && ! grep -q '^done' "$work/observed.4" 2>/dev/null; do
		sleep 0.1; i=$((i + 1))
	done

	[ -s "$work/observed.4" ] || fail \
		"the peer recorded nothing at all, so this case proved nothing about
what a refused entry sends. Expected a connection that says 'eof'."

	! grep -q '^got ' "$work/observed.4" || fail \
		"a refused definition sent its command anyway. The parser turned the
entry down, and the probe spoke it to the server regardless -- on an entry
that logs in, that is a rejected config putting credentials on the wire:
$(cat "$work/observed.4")"

	# --- 4b. and a refused entry does not open TLS either -------------------
	#
	# TLS starts from the service flags, before any step runs, so "options
	# ssl,bogus" still put a ClientHello on the wire. The peer speaks no TLS, so
	# the attempt turns the yellow refusal into a red SSL error.
	printf '%s\n' 'recvany' 'hangup' > "$work/peer.script"
	port=$(new_peer 4b)

	printf '[mutetls]\n   expect "220"\n   options ssl,bogusopt\n   port %s\n' \
		"$port" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\thost4b\t# mutetls\n' > "$work/home/etc/hosts.cfg"

	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 2>&1 || :)
	colour=$(grep -oE 'status\+[0-9]+ host4b\.mutetls (green|yellow|red|clear)' <<<"$out" | awk '{print $3}' | head -1)

	i=0; while [ "$i" -lt 50 ] && ! grep -q '^done' "$work/observed.4b" 2>/dev/null; do
		sleep 0.1; i=$((i + 1))
	done

	! grep -q '^got ' "$work/observed.4b" || fail \
		"a refused entry opened a TLS connection. The definition was turned down
and a ClientHello went out from the ssl half of it anyway:
$(cat "$work/observed.4b")"

	[ "$colour" = "yellow" ] || fail \
		"a refused entry with 'options ssl' reported '$colour', not yellow. The
handshake it should never have attempted fails against a plaintext peer, so
the column names an SSL error instead of the refusal that is the real fault:
$(grep -iE 'mutetls' <<<"$out" | head -3)"

	# --- 5. and a refusal is not conditional on --checkresponse -------------
	#
	# That gate is off by default and is right for an expect mismatch, which is
	# a judgement. A definition rejected when the file was read is not: no check
	# ran, so green claims one happened.
	printf '%s\n' 'send "220 test.local ESMTP\r\n"' 'recvany' 'send "250 ok\r\n"' 'hold 3' \
		> "$work/peer.script"
	port=$(new_peer 5)
	printf '[nocheck]\n   expect "220"\n   start tsl\n   options banner\n   port %s\n' \
		"$port" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\thost5\t# nocheck\n' > "$work/home/etc/hosts.cfg"

	out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping \
		--dns=ip --timeout=10 2>&1 || :)
	colour=$(grep -oE 'status\+[0-9]+ host5\.nocheck (green|yellow|red|clear)' <<<"$out" | awk '{print $3}' | head -1)

	[ "$colour" != "green" ] || fail \
		"a refused entry reported the service UP when --checkresponse was not
given. Without it no expect is consulted, and the refusal is delivered
through that same door -- so the default configuration is the one where a
rejected definition is invisible:
$(grep -iE 'nocheck|can start' <<<"$out" | head -4)"
fi

pass "a mistyped directive is reported when the file is read, the shipped file reports none, and neither a refused feature nor a mistyped directive reports the service up"
