# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/lib/mkpeer.awk
#
# Build, from a protocols configuration, a peer script per service: the least a
# server can say and still satisfy that entry. Written from the entry itself so
# the two cannot drift -- a peer that had to be maintained by hand would stop
# speaking its protocol the first time an entry changed, and every result taken
# against it would quietly become meaningless.
#
# usage: awk -v dir=DIR -f mkpeer.awk protocols2.cfg
#
# Rules taken from the grammar being modelled:
#   send        the server waits: "recvraw"
#   expect      the server answers with something the literal matches
#   ... at N    the literal sits at byte N, so N bytes of filler precede it
#   ... until X a multi-line reply: this line, then one starting with X
#   state       a boundary. Expects in ONE state are alternatives and a server
#               answers once; expects in DIFFERENT states are a sequence and
#               each gets its own reply.
#
# Entries needing TLS are skipped: a plaintext peer cannot complete their
# handshake, so a result against one would say nothing about the protocol.

function flush_entry() {
	if (name == "") return
	if (ssl || tls) { skipped = skipped " " first; name = ""; return }
	if (nl == 0) lines[++nl] = "send \"x ok\\r\\n\""   # connect-only: say something
	lines[++nl] = "hangup"
	out = dir "/" first ".peer"
	for (i = 1; i <= nl; i++) print lines[i] > out
	close(out)
	print first
	name = ""
}

BEGIN { name = ""; skipped = "" }

/^\[/ {
	flush_entry()
	name = substr($0, 2, index($0, "]") - 2)
	first = name; sub(/\|.*/, "", first)
	nl = 0; ssl = 0; tls = 0; prev_expect = 0
	next
}

/^[ \t]*#/ { next }
/^[ \t]*options[ \t]/ { if ($0 ~ /ssl/) ssl = 1; next }
/^[ \t]*start[ \t]+tls/ { tls = 1; next }
/^[ \t]*state[ \t]/ { prev_expect = 0; next }
/^[ \t]*send[ \t]/ { lines[++nl] = "recvraw"; prev_expect = 0; next }

/^[ \t]*expect[ \t]/ {
	if (prev_expect) next               # an alternative of a state already answered
	n = split($0, q, "\"")
	lit = (n >= 2) ? q[2] : ""
	untl = ""
	if ($0 ~ /until[ \t]+"/ && n >= 4) untl = q[4]
	at = ""
	if (match($0, /at[ \t]+[0-9]+/)) {
		at = substr($0, RSTART, RLENGTH); sub(/at[ \t]+/, "", at)
	}

	pay = ""
	if (at != "") { for (i = 0; i < at + 0; i++) pay = pay "\\x2e" }
	pay = pay ((lit == "") ? "x" : lit)
	if (untl != "")     pay = pay "-first\\r\\n" untl "done\\r\\n"
	else if (at == "")  pay = pay " ok\\r\\n"

	lines[++nl] = "send \"" pay "\""
	prev_expect = 1
	next
}

END {
	flush_entry()
	if (skipped != "") print "SKIPPED:" skipped > "/dev/stderr"
}
