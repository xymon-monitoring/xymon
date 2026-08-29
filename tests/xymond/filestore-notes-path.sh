#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/filestore-notes-path.sh
#
# Regression guard for the two update_file() defects in xymond_filestore.
#
# 1. The @@notes handler built its destination as basename(filedir)/hostname
#    -- a cwd-relative base, from the basename of the configured directory --
#    with the host part taken verbatim from the message. xymond deliberately
#    does not validate the ID on notes messages.
#
# 2. update_file() wrote through its fopen() handle without checking it. A
#    destination that cannot be opened for writing therefore reached
#    fwrite(msg, len, 1, NULL) and crashed the worker. A "." ID reaches
#    exactly that: update_file() writes to "$dir/.$base", so "." lands on
#    the directory itself, which fopen(..., "w") refuses.
#
# Workers read their messages from stdin (xymond_worker.c), so this drives
# the real binary and checks the filesystem.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

ROOT=$(find_root)

# build_xymond_worker resolves GNU make, the configured compile and link flags
# and the library set for us. Hand-rolling those meant literal `make`, which
# BSD make cannot parse on the generated GNU Makefiles, so this test failed
# before reaching an assertion on FreeBSD, NetBSD and OpenBSD.
work=$(mktempdir)
build_xymond_worker "$work" xymond_filestore \
	xymond/xymond_filestore.c xymond/xymond_worker.c

mkdir -p "$work/etc"
cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

setup() {
	rm -rf "$work/var"
	mkdir -p "$work/var/notes" "$work/var/sibling" "$work/var/logs" "$work/var/html"
	printf 'CANARY-OUTSIDE-NOTES\n' >"$work/var/CANARY.txt"
	printf 'sibling content\n'      >"$work/var/sibling/keepme"
}

notes() {  # notes <id>
	local rc
	set +e
	printf '@@notes#1|1785830400|10.0.0.99|%s\nNOTE PAYLOAD\n@@\n' "$1" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
		--notes --dir="$work/var/notes" >/dev/null 2>>"$work/worker.log"
	rc=$?
	set -e
	# 0 is the normal exit when the input pipe closes. A crash (139) is how
	# the unchecked fopen() handle failed, so it must not read as "refused".
	[ "$rc" -le 1 ] || fail "xymond_filestore exited $rc (crash?) on notes ID '$1'"
}

# Sanity first: if nothing is ever written here, every "did not escape"
# assertion below holds vacuously.
setup
: >"$work/worker.log"
notes "realhost"
[ -f "$work/var/notes/realhost" ] \
	|| { cat "$work/worker.log" >&2; fail "a legitimate notes message was not stored -- the checks below would pass vacuously"; }
assert_contains "NOTE PAYLOAD" "$(cat "$work/var/notes/realhost")" "notes payload not stored"

# No ID may put a file outside the configured notes directory, and none may
# crash the worker.
for id in ".." "." "../evil" "/etc/passwd" "a/b" "//"; do
	setup
	notes "$id"
	escaped=$(find "$work/var" -maxdepth 1 -type f ! -name 'CANARY.txt' | head -1)
	[ -z "$escaped" ] || fail "notes ID '$id' wrote outside the notes directory: $escaped"
	[ -f "$work/var/CANARY.txt" ] || fail "notes ID '$id' clobbered a file outside the notes directory"
	[ -f "$work/var/sibling/keepme" ] || fail "notes ID '$id' touched a sibling directory"
done

# ---- an oversized ID is refused, and one that fits exactly does not overflow -
#
# The notes ID has no length check anywhere before this worker: xymond accepts
# a 64 MiB message, the notes channel carries 256 KiB, and posttochannel()
# copies the whole ID through.
#
# Two different sizes fail in two different ways. A grossly long one must be
# refused outright, leaving the directory as it was rather than truncated into
# some other note's name.
#
# 5000 bytes overflows the PATH_MAX buffer the name is built into, so the notes
# handler's snprintf bound refuses it before the filesystem is touched -- and it
# does so on every platform, since 5000 exceeds PATH_MAX everywhere (1024 on the
# BSDs, 4096 on Linux). An earlier version built a >3900-byte scratch directory
# here to force that; it was never used by the message below and only broke the
# BSD lanes, where mkdir of such a path fails under set -e (@SoundGoof).
before=$(ls "$work/var/notes" | sort | tr '\n' ' ')
notes "$(printf 'a%.0s' $(seq 1 5000))"
after=$(ls "$work/var/notes" | sort | tr '\n' ' ')

assert_equal "$before" "$after" \
	"an oversized notes ID changed the notes directory -- it must be refused, not truncated into some other note's name"
assert_file_exists "$work/var/CANARY.txt" \
	"an oversized notes ID reached outside the notes directory"

# The overflow @SoundGoof reported under ASan (WRITE of size 4097) is not that
# one, and asserting on a refusal cannot show it either way: the write is past
# a stack buffer, which a plain build survives silently. Every caller builds
# the destination with a bounded snprintf() into a PATH_MAX buffer, so 5000
# bytes never reaches update_file(). What does reach it is a destination that
# fits PATH_MAX *exactly*: update_file() inserts a '.' before the basename, so
# the temporary name is one byte longer than the name it is derived from, and
# at PATH_MAX-1 that byte is one too many. Accounting for it is the whole of
# the fix, so the case is worth pinning even though it needs a second build.
if asan_usable; then
	asanwork=$(mktempdir)
	# A wrapper rather than CC="cc -fsanitize=...": build_xymond_worker
	# resolves $CC with command -v, which cannot see a multi-word string.
	cat >"$asanwork/cc-asan" <<EOF
#!/bin/sh
exec ${CC:-cc} -fsanitize=address -g "\$@"
EOF
	chmod +x "$asanwork/cc-asan"
	CC="$asanwork/cc-asan" build_xymond_worker "$asanwork" xymond_filestore \
		xymond/xymond_filestore.c xymond/xymond_worker.c

	mkdir -p "$asanwork/etc" "$asanwork/var/notes"
	cp "$work/etc/xymonserver.cfg" "$asanwork/etc/xymonserver.cfg"

	# The buffers are PATH_MAX from <limits.h>, so ask the compiler instead of
	# assuming 4096: a wrong guess would quietly stop reaching the overflow and
	# leave this half asserting nothing.
	printf '#include <limits.h>\n#include <stdio.h>\nint main(void){printf("%%d\\n", PATH_MAX);return 0;}\n' \
		>"$asanwork/pathmax.c"
	"${CC:-cc}" -o "$asanwork/pathmax" "$asanwork/pathmax.c" 2>/dev/null \
		|| fail "cannot determine PATH_MAX"
	pathmax=$("$asanwork/pathmax")

	notesdir="$asanwork/var/notes"
	# "<notesdir>/<id>" of exactly PATH_MAX-1: the longest destination that is
	# not refused, hence the one whose derived ".<id>" no longer fits.
	idlen=$(( pathmax - 1 - ${#notesdir} - 1 ))
	[ "$idlen" -gt 0 ] || fail "the work directory is too deep to build a PATH_MAX-1 notes path"

	set +e
	printf '@@notes#1|1785830400|10.0.0.99|%s\nNOTE PAYLOAD\n@@\n' \
		"$(printf 'a%.0s' $(seq 1 "$idlen"))" \
	| XYMONHOME="$asanwork" XYMONVAR="$asanwork/var" \
		"$asanwork/xymond_filestore" --env="$asanwork/etc/xymonserver.cfg" \
		--notes --dir="$notesdir" >/dev/null 2>"$asanwork/asan.log"
	rc=$?
	set -e

	if grep -q AddressSanitizer "$asanwork/asan.log"; then
		sed -n '1,12p' "$asanwork/asan.log" >&2
		fail "a notes ID whose destination fills PATH_MAX exactly overflowed the temporary name"
	fi
	# ASan aborting without leaving its banner in the log -- a plain crash, or a
	# report written elsewhere -- would slip past the grep. The normal exit is 0
	# (pipe closed); anything past 1 is the overflow crashing the worker.
	[ "$rc" -le 1 ] || fail "the exact-fit notes worker exited $rc (crash?) with no ASan banner"
else
	printf 'note: %s cannot build and run ASan binaries, the overflow itself is unverified\n' \
		"${CC:-cc}" >&2
fi


# ---- the "--status --html" role writes a second file, from the same name ----
# It inserts the hostname raw into "$htmldir/<host>.<test>.html", so a '/'
# in the name lands the file outside htmldir. Shipped DISABLED in tasks.cfg
# ("storestatus"), but the code path is the same class as the one above.
status_html() {  # status_html <hostname> [testname]
	local rc
	set +e
	printf '@@status#1|1785830400.000000|10.0.0.99|origin|%s|%s|0|green|flags|green|1785830000|0||0||0|0\nSTATUS BODY\n@@\n' "$1" "${2:-conn}" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
		--status --dir="$work/var/logs" --htmldir="$work/var/html" \
		>/dev/null 2>>"$work/worker.log"
	rc=$?
	set -e
	[ "$rc" -le 1 ] || fail "xymond_filestore exited $rc (crash?) on status '$1'/'${2:-conn}'"
}

setup
status_html "realhost"
[ -f "$work/var/html/realhost.conn.html" ] \
	|| { cat "$work/worker.log" >&2; fail "a legitimate status HTML log was not written -- the checks below would pass vacuously"; }
# Both halves of the name must survive into it. basename(3) may return storage
# that the next call overwrites -- NetBSD and OpenBSD do -- so building the
# name with two calls inside one snprintf() left both arguments naming the same
# string and stored realhost/conn as conn.conn (measured before basename() was
# retired for refusal). Kept: any future reduction of the components has to
# preserve both halves, whatever it is built from.
[ -f "$work/var/logs/realhost.conn" ] \
	|| fail "the raw status log was not written as <host>.<test>: $(find "$work/var/logs" -type f)"
[ ! -f "$work/var/logs/conn.conn" ] \
	|| fail "both name components came out as the test name -- basename() results aliased"

# ---- a negative "time since change" must not print uninitialised bytes ------
# update_htmlfile()'s timestr is function-scoped and handed to
# generate_html_log() unconditionally, which prints "Status unchanged in <it>"
# when it is non-empty. The block that fills it runs only for a non-negative
# delta; a changetime later than the log time (a clock step) makes the delta
# negative and skips it, so an uninitialised timestr leaks into the served
# page. Two messages in one worker run: the first (large positive delta) dirties
# the stack slot, the second (negative delta) must still print no such line.
setup
{
	printf '@@status#1|1785830000.000000|10.0.0.99|origin|realhost|oldone|0|red|f|red|1700000000|0||0||0|0\nBODY1\n@@\n'
	printf '@@status#2|1785830000.000000|10.0.0.99|origin|realhost|negdelta|0|red|f|red|1785830400|0||0||0|0\nBODY2\n@@\n'
} | XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" --htmldir="$work/var/html" >/dev/null 2>>"$work/worker.log"
assert_contains "Status unchanged" "$(cat "$work/var/html/realhost.oldone.html")" \
	"the positive-delta page should carry the unchanged-for line"
assert_not_contains "Status unchanged" "$(cat "$work/var/html/realhost.negdelta.html")" \
	"a negative delta must leave timestr empty, not print uninitialised stack into the page"
[ ! -f "$work/var/html/conn.conn.html" ] \
	|| fail "both name components came out as the test name in the HTML log"

for host in ".." "../evil" "a/b" "/etc/passwd"; do
	setup
	status_html "$host"
	stray=$(find "$work/var" -maxdepth 1 -name '*.html' | head -1)
	[ -z "$stray" ] || fail "status hostname '$host' wrote an HTML log outside htmldir: $stray"
	[ -f "$work/var/CANARY.txt" ] || fail "status hostname '$host' clobbered a file outside htmldir"
	[ -f "$work/var/sibling/keepme" ] || fail "status hostname '$host' touched a sibling directory"
done

# The test name is inserted into both the raw status path and the HTML one and
# is no more trustworthy than the hostname: nothing upstream on the status
# channel rejects a slash in it.
for test in ".." "../evil" "a/b" "/etc/passwd" "../../../escaped"; do
	setup
	status_html "realhost" "$test"
	stray=$(find "$work/var" -maxdepth 1 \( -name '*.html' -o -name 'escaped*' -o -name 'evil*' \) | head -1)
	[ -z "$stray" ] || fail "status testname '$test' wrote outside the configured directories: $stray"
	[ -f "$work/var/CANARY.txt" ] || fail "status testname '$test' clobbered a file outside htmldir"
	[ -f "$work/var/sibling/keepme" ] || fail "status testname '$test' touched a sibling directory"
done

# ---- a '/' component is refused, never reduced onto another file's name -----
# basename() confinement mapped "a/victim" onto "victim": a different wire
# identifier resolved to an existing file inside the directory and replaced it
# (@SoundGoof). The escape cases above cannot see this -- the write lands
# inside the configured directory. Seed the legitimate destination, drive the
# malformed name, and the destination must be untouched with nothing new
# beside it.
setup
printf 'NOTE-LEGITIME\n' > "$work/var/notes/victim"
notes "a/victim"
assert_equal "NOTE-LEGITIME" "$(cat "$work/var/notes/victim")" \
	"notes ID 'a/victim' was reduced onto the note called 'victim' and replaced it"
assert_equal "1" "$(find "$work/var/notes" -type f | awk 'END { print NR }')" \
	"notes ID 'a/victim' was stored under some reduced name"

# The status role reduced the same way, and writes the HTML page from the same
# name -- one message replaced both the log and the served page.
setup
printf 'LOGDATA\n'  > "$work/var/logs/victim.conn"
printf 'PAGEDATA\n' > "$work/var/html/victim.conn.html"
status_html "a/victim" conn
assert_equal "LOGDATA" "$(cat "$work/var/logs/victim.conn")" \
	"status host 'a/victim' was reduced onto victim.conn and replaced the log"
assert_equal "PAGEDATA" "$(cat "$work/var/html/victim.conn.html")" \
	"status host 'a/victim' was reduced onto victim.conn.html and replaced the page"
assert_equal "1" "$(find "$work/var/logs" -type f | awk 'END { print NR }')" \
	"status host 'a/victim' left a log under some reduced name"

# renamehost reduced its target too: renaming oldhost to "a/victim" replaced
# victim.conn and removed oldhost.conn. Refused, both originals stay.
setup
printf 'LOGDATA-OLD\n'    > "$work/var/logs/oldhost.conn"
printf 'LOGDATA-VICTIM\n' > "$work/var/logs/victim.conn"
printf '@@renamehost#1|1785830400|10.0.0.99|oldhost|a/victim\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "LOGDATA-VICTIM" "$(cat "$work/var/logs/victim.conn")" \
	"renamehost to 'a/victim' was reduced onto victim.conn and replaced it"
assert_equal "LOGDATA-OLD" "$(cat "$work/var/logs/oldhost.conn")" \
	"renamehost to a refused name must leave the original log alone"

# And renametest, through its new-test half.
setup
printf 'LOGDATA-OLD\n'    > "$work/var/logs/realhost.conn"
printf 'LOGDATA-VICTIM\n' > "$work/var/logs/realhost.http"
printf '@@renametest#1|1785830400|10.0.0.99|realhost|conn|a/http\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "LOGDATA-VICTIM" "$(cat "$work/var/logs/realhost.http")" \
	"renametest to 'a/http' was reduced onto realhost.http and replaced it"
assert_equal "LOGDATA-OLD" "$(cat "$work/var/logs/realhost.conn")" \
	"renametest to a refused name must leave the original log alone"

# ---- ".", ".." and "" are names in their own right, and refused ------------
# None is a host or a test. Only the components that skip the dot-to-comma pass
# can carry a literal "." or ".." this far -- the notes ID and the test names;
# a channelled hostname has its dots turned to commas first. The filesystem
# outcome alone cannot show the fix (a note called "." fell over at open(), ".."
# at rename() either way), so the log assertion is what pins it: revert the
# guard and the refusal is gone while the files still look fine.
for id in "." ".."; do
	setup
	printf 'NOTE-LEGITIME\n' > "$work/var/notes/victim"
	before=$(ls -a "$work/var/notes" | sort | tr '\n' ' ')
	: >"$work/worker.log"
	notes "$id"
	assert_equal "$before" "$(ls -a "$work/var/notes" | sort | tr '\n' ' ')" \
		"notes ID '$id' changed the notes directory -- it must be refused as a name"
	assert_contains "NOTE-LEGITIME" "$(cat "$work/var/notes/victim")" \
		"notes ID '$id' disturbed an existing note"
	grep -q 'begins with a dot' "$work/worker.log" \
		|| fail "notes ID '$id' was not refused by name -- it only failed at a later syscall"
done

# ---- a leading-dot ID is refused: it collides with the temp namespace -------
# update_file() derives its temp as the destination with a '.' prepended, so a
# stored ".victim" is the exact temp name it computes for "victim". Before the
# leading-dot refusal, storing ".victim" (reachable as wire ",victim", which
# xymond uncommafies) and then writing "victim" made update_file()'s unlink of
# its temp destroy the ".victim" note. Now ".victim" never lands at all.
setup
: >"$work/worker.log"
notes ".victim"
[ ! -e "$work/var/notes/.victim" ] \
	|| fail "a notes ID beginning with '.' was stored: $(ls -la "$work/var/notes/.victim")"
grep -q 'begins with a dot' "$work/worker.log" \
	|| fail "a leading-dot notes ID was not refused by name"
notes "victim"
assert_contains "NOTE PAYLOAD" "$(cat "$work/var/notes/victim")" \
	"a legitimate 'victim' note was not stored after a refused '.victim'"
[ ! -e "$work/var/notes/.victim" ] \
	|| fail "writing 'victim' recreated the '.victim' temp as a lingering file"

# ---- a '\\' component is refused, like '/' ----------------------------------
# xymond admits '\\' in a new test name; on a '\\'-separated ($XYMONVAR on
# SMB/CIFS) mount it is a directory separator, so name_confined() refuses it.
setup
: >"$work/worker.log"
status_html "realhost" 'a\b'
[ -z "$(find "$work/var/logs" "$work/var/html" -type f)" ] \
	|| fail "a status test name with '\\' wrote a file: $(find "$work/var/logs" "$work/var/html" -type f)"
grep -q 'path separator' "$work/worker.log" \
	|| fail "a test name holding '\\' was not refused by name"

# The test name skips the pass too: a "." test name reaches the same guard
# through build_logfn().
setup
: >"$work/worker.log"
status_html "realhost" "."
[ -z "$(find "$work/var/logs" "$work/var/html" -type f)" ] \
	|| fail "a status test name '.' wrote a file: $(find "$work/var/logs" "$work/var/html" -type f)"
grep -q 'begins with a dot' "$work/worker.log" \
	|| fail "a status test name '.' was not refused by name"

# The empty-name branch is reached where the empty field is not the last one:
# a status with an empty test name keeps metacount up and builds "<host>." .
# Sent raw, not through status_html() -- its "${2:-conn}" would fill the field
# in. Refused, no file is written under the host and the refusal is logged.
setup
: >"$work/worker.log"
printf '@@status#1|1785830400.000000|10.0.0.99|origin|realhost||0|green|flags|green|1785830000|0||0||0|0\nSTATUS BODY\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" --htmldir="$work/var/html" >/dev/null 2>>"$work/worker.log"
[ -z "$(find "$work/var/logs" "$work/var/html" -type f)" ] \
	|| fail "a status with an empty test name wrote a file: $(find "$work/var/logs" "$work/var/html" -type f)"
grep -q 'Empty name' "$work/worker.log" \
	|| fail "a status with an empty test name was not refused by name"

# drophost and renamehost match by prefix, so an empty source host makes the
# lead "." and reaches every dotfile -- the ".host.test" temp files and any
# other. Empty is reachable when the field is not the last: "@@drophost|..||x"
# keeps the message from being dropped for metacount. The dot entries and a
# legitimate log must be left alone, and the refusal logged.
setup
printf 'LOGDATA\n' > "$work/var/logs/realhost.conn"
printf 'DOTFILE\n'  > "$work/var/logs/.hidden"
: >"$work/worker.log"
printf '@@drophost#1|1785830400|10.0.0.99||extra\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
[ -f "$work/var/logs/.hidden" ] \
	|| fail "an empty drophost source host removed a dotfile it must not match"
[ -f "$work/var/logs/realhost.conn" ] \
	|| fail "an empty drophost source host removed a legitimate log"
grep -q 'Empty name' "$work/worker.log" \
	|| fail "an empty drophost source host was not refused by name"

setup
printf 'LOGDATA\n' > "$work/var/logs/realhost.conn"
printf 'DOTFILE\n'  > "$work/var/logs/.hidden"
: >"$work/worker.log"
printf '@@renamehost#1|1785830400|10.0.0.99||newhost\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
[ -f "$work/var/logs/.hidden" ] \
	|| fail "an empty renamehost source host renamed a dotfile it must not match"
[ -z "$(find "$work/var/logs" -name 'newhost.*')" ] \
	|| fail "an empty renamehost source host produced a renamed file: $(find "$work/var/logs" -name 'newhost.*')"
grep -q 'Empty name' "$work/worker.log" \
	|| fail "an empty renamehost source host was not refused by name"

# update_file() writes through a predictable temp name, ".<basename>", beside
# the destination. Opened with fopen() that follows a symlink planted there, so
# anyone able to create a file in the notes directory could have the daemon
# truncate and write any file it can reach -- and the rename() then moves the
# symlink into place as the real file.
setup
ln -s ../CANARY.txt "$work/var/notes/.seeded"
notes "seeded"
assert_contains "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a symlink planted at the temp name let the note overwrite a file outside notes/"
[ ! -L "$work/var/notes/seeded" ] \
	|| fail "the planted symlink was renamed into place as the note file"

# O_NOFOLLOW stops a symlink but not a hardlink: a hardlink planted at the temp
# name is a second name for the target's own inode, so opening it and writing
# (or O_TRUNC emptying it) reaches the target with no link to follow. The temp
# is unlinked before it is created, which breaks the hardlink first.
setup
ln "$work/var/CANARY.txt" "$work/var/notes/.seeded"   # hardlink, not symlink
notes "seeded"
assert_equal "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a hardlink planted at the temp name let the note overwrite the file it linked"
assert_contains "NOTE PAYLOAD" "$(cat "$work/var/notes/seeded")" \
	"the note was not written after the hardlink was cleared"

# The HTML writer has its own temp name, "<destination>.tmp", and had the same
# hole: the page is what the web server serves, so a symlink planted there let
# the daemon rewrite any file it can reach and then publish the symlink in its
# place (@SoundGoof).
setup
status_html "realhost"   # the page has to exist before it can be replaced
[ -f "$work/var/html/realhost.conn.html" ] \
	|| { cat "$work/worker.log" >&2; fail "the HTML page was not written -- the case below would prove nothing"; }
ln -s ../CANARY.txt "$work/var/html/realhost.conn.html.tmp"
status_html "realhost"
assert_contains "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a symlink planted at the HTML temp name let the page overwrite a file outside htmldir"
[ ! -L "$work/var/html/realhost.conn.html" ] \
	|| fail "the planted symlink was renamed into place as the HTML page"
rm -f "$work/var/html/realhost.conn.html.tmp"

# The HTML temp has the same hardlink exposure as the notes temp.
setup
status_html "realhost"
[ -f "$work/var/html/realhost.conn.html" ] \
	|| { cat "$work/worker.log" >&2; fail "the HTML page was not written -- the case below would prove nothing"; }
ln "$work/var/CANARY.txt" "$work/var/html/realhost.conn.html.tmp"   # hardlink
status_html "realhost"
assert_equal "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a hardlink planted at the HTML temp name let the page overwrite the file it linked"
rm -f "$work/var/html/realhost.conn.html.tmp"

# The data role opens its temp in append mode. A leftover ".host.test" from a
# crashed run was reused: the new sample was appended to the stale bytes and
# the pair renamed over the real file. Unlinking the temp before creating it
# drops the leftover, so what is published is only the new sample.
setup
mkdir -p "$work/var/data"
printf 'STALE-TEMP\n' > "$work/var/data/.realhost.conn"   # a crash leftover
printf '@@data#1|1785830400|10.0.0.99|origin|realhost|conn\nFRESH-SAMPLE\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--data --dir="$work/var/data" >/dev/null 2>>"$work/worker.log"
published=$(cat "$work/var/data/realhost.conn" 2>/dev/null)
assert_contains "FRESH-SAMPLE" "$published" "the fresh data sample was not published"
case "$published" in
	*STALE-TEMP*) fail "a crash-leftover temp was appended to and published: [$published]" ;;
esac

# The enable/disable role writes too: it creates the file for its timestamp
# alone, which is still a truncation of whatever a planted symlink points at.
# The third writer in this file with the same shape -- found by asking which
# others had it, rather than by being told about this one.
setup
mkdir -p "$work/var/logs"
ln -s ../CANARY.txt "$work/var/logs/realhost.conn"
printf '@@enadis#1|1785830400|10.0.0.99|realhost|conn|%s\n@@\n' "$(( $(date +%s) + 3600 ))" \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--enadis --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_contains "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a symlink planted at the disable-file name let the worker truncate a file outside its directory"

# A link planted at the final disable-file name is only ever the target of a
# rename(), which does not follow it -- so this proves the final name is safe,
# but not that the open is. update_enable() writes a temp and renames it now.
setup
mkdir -p "$work/var/logs"
ln "$work/var/CANARY.txt" "$work/var/logs/realhost.conn"   # hardlink at the final name
printf '@@enadis#1|1785830400|10.0.0.99|realhost|conn|%s\n@@\n' "$(( $(date +%s) + 3600 ))" \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--enadis --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a hardlink planted at the disable-file name let the worker truncate the file it linked"

# The open now happens on the temp, ".realhost.conn", so the plant belongs
# there: a symlink or a hardlink at the temp is unlinked before the O_EXCL
# create, not written through. These are what pin update_enable()'s temp open
# -- swap it back to an fopen() or drop the unlink and CANARY is overwritten.
setup
mkdir -p "$work/var/logs"
ln -s ../CANARY.txt "$work/var/logs/.realhost.conn"   # symlink at the temp
printf '@@enadis#1|1785830400|10.0.0.99|realhost|conn|%s\n@@\n' "$(( $(date +%s) + 3600 ))" \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--enadis --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_contains "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a symlink planted at the disable-file temp let the worker write through it"
[ -f "$work/var/logs/realhost.conn" ] && [ ! -L "$work/var/logs/realhost.conn" ] \
	|| fail "the disable-file was not created as a plain file after the planted temp symlink"

setup
mkdir -p "$work/var/logs"
ln "$work/var/CANARY.txt" "$work/var/logs/.realhost.conn"   # hardlink at the temp
printf '@@enadis#1|1785830400|10.0.0.99|realhost|conn|%s\n@@\n' "$(( $(date +%s) + 3600 ))" \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--enadis --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "CANARY-OUTSIDE-NOTES" "$(cat "$work/var/CANARY.txt")" \
	"a hardlink planted at the disable-file temp let the worker truncate the file it linked"

# And the role still does its job -- the disable file is created and no temp is
# left behind. update_enable() writes a temp and renames it (so the real name
# is never briefly absent, unlike the earlier unlink-in-place); the "." temp
# must be gone once the rename published it.
setup
printf '@@enadis#1|1785830400|10.0.0.99|realhost|conn|%s\n@@\n' "$(( $(date +%s) + 3600 ))" \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--enadis --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
[ -f "$work/var/logs/realhost.conn" ] \
	|| fail "an ordinary disable message created no file -- the symlink case above would pass on a worker that writes nothing"
[ ! -e "$work/var/logs/.realhost.conn" ] \
	|| fail "update_enable left its temp behind: $(ls -l "$work/var/logs/.realhost.conn")"

# A rename driven from the channel builds both names from message fields, which
# are not length-checked anywhere before this worker. The originals must be
# left alone rather than renamed through a name that did not fit.
setup
mkdir -p "$work/var/logs"
printf 'x\n' > "$work/var/logs/oldhost.conn"
huge=$(printf 'n%.0s' $(seq 1 5000))
set +e
printf '@@renamehost#1|1785830400|10.0.0.99|oldhost|%s\n@@\n' "$huge" \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
rc=$?
set -e
# Named rather than left as a bare exit status: an unbounded build of this name
# aborts the worker (134, stack smashing), and a test that only looked at the
# files afterwards would report the wrong thing about a process that died.
[ "$rc" -le 1 ] \
	|| fail "xymond_filestore exited $rc on an oversized rename target -- the name was built without a bound"
[ -f "$work/var/logs/oldhost.conn" ] \
	|| fail "an oversized rename target took the original log file with it"
assert_equal "1" "$(find "$work/var/logs" -type f | awk 'END { print NR }')" \
	"the oversized rename left something behind in the log directory"

# The rename target is a message field too, and the dot-to-comma pass over it
# stops ".." but not "/": "link/moved" renamed the log through a symlinked
# directory and out of the configured one (measured).
setup
mkdir -p "$work/var/outside"
ln -s ../outside "$work/var/logs/link"
printf 'LOGDATA\n' > "$work/var/logs/oldhost.conn"
printf '@@renamehost#1|1785830400|10.0.0.99|oldhost|link/moved\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "0" "$(find "$work/var/outside" -type f | awk 'END { print NR }')" \
	"a rename target containing '/' moved the log through a symlinked directory and out of the configured one"

# The positive control for both renamehost cases.
setup
printf 'LOGDATA\n' > "$work/var/logs/oldhost.conn"
printf '@@renamehost#1|1785830400|10.0.0.99|oldhost|newhost\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
[ -f "$work/var/logs/newhost.conn" ] \
	|| fail "an ordinary host rename did not happen -- the two cases above would pass on a worker that renames nothing"

# The rename target is not put through the dot-to-comma pass the hostnames get,
# so it is the one field where a "/" and a ".." both survive: "a/../../escaped"
# moved the log clean out of the directory.
setup
mkdir -p "$work/var/logs/realhost.a"
printf 'LOGDATA\n' > "$work/var/logs/realhost.conn"
printf '@@renametest#1|1785830400|10.0.0.99|realhost|conn|a/../../escaped\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "0" "$(find "$work/var" -maxdepth 1 -name 'escaped*' | awk 'END { print NR }')" \
	"a rename target containing '..' moved the log out of the configured directory"

# The destination has two halves, and confining one is not confining it: the
# hostname's dots become commas, which stops "..", but a '/' survives there
# too. Measured before the fix -- the log left the directory through a
# symlinked one even with the test name already confined.
setup
mkdir -p "$work/var/outside"
ln -s ../outside "$work/var/logs/link"
printf 'LOGDATA\n' > "$work/var/logs/oldhost.conn"
printf '@@renametest#1|1785830400|10.0.0.99|link/oldhost|conn|http\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
assert_equal "0" "$(find "$work/var/outside" -type f | awk 'END { print NR }')" \
	"a rename whose hostname contains '/' moved the log through a symlinked directory and out of the configured one"

# And a legitimate rename still happens, or the case above passes on a worker
# that renames nothing at all.
setup
printf 'LOGDATA\n' > "$work/var/logs/realhost.conn"
printf '@@renametest#1|1785830400|10.0.0.99|realhost|conn|http\n@@\n' \
| XYMONHOME="$work" XYMONVAR="$work/var" \
	"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
	--status --dir="$work/var/logs" >/dev/null 2>>"$work/worker.log"
[ -f "$work/var/logs/realhost.http" ] \
	|| fail "an ordinary rename did not happen -- the traversal case above would pass on a worker that renames nothing"

# An oversized ID is refused, not shortened. The cut lands wherever the length
# says: after a '/' it left a short, valid name that is not this note's, which
# the assembled-path check cannot tell apart. Measured before the fix: this
# overwrote the note called "victim".
setup
printf 'NOTE-LEGITIME\n' > "$work/var/notes/victim"
before=$(ls "$work/var/notes" | sort | tr '\n' ' ')
notes "$(printf 'a%.0s' $(seq 1 4088))/victim$(printf 'X%.0s' $(seq 1 500))"
assert_contains "NOTE-LEGITIME" "$(cat "$work/var/notes/victim")" \
	"an oversized notes ID was truncated onto another note's name and overwrote it"
assert_equal "$before" "$(ls "$work/var/notes" | sort | tr '\n' ' ')" \
	"an oversized notes ID left a file behind under a truncated name"

# The status role takes its two components in the same kind of copy, so it has
# the same cut. Driven through the host name, with the log directory watched
# rather than the notes one.
setup
printf 'LOGDATA\n' > "$work/var/logs/victim.conn"
status_html "$(printf 'a%.0s' $(seq 1 4088))/victim$(printf 'X%.0s' $(seq 1 500))" conn
assert_contains "LOGDATA" "$(cat "$work/var/logs/victim.conn")" \
	"an oversized status hostname was truncated onto another host's log and overwrote it"
assert_equal "1" "$(find "$work/var/logs" -type f | awk 'END { print NR }')" \
	"an oversized status hostname left a file behind under a truncated name"

# ---- a write that cannot finish must not replace what is already there ------
# The temp file is renamed over the destination once the stream closes. A
# failed fwrite() was logged and then ignored, and fclose() reports nothing
# when the failure already emptied the buffer -- so a note that could not be
# written whole replaced a good one with however much fitted. Reproduced with
# a write limit; a full filesystem is the same shape.
setup
notes "limited"
orig=$(cat "$work/var/notes/limited")
assert_contains "NOTE PAYLOAD" "$orig" "the note to be protected was not stored"

big=$(awk 'BEGIN { while (i++ < 4000) printf "X" }')
# set +e around the subshell as every other worker call here does: a nonzero
# subshell exit (a worker crash, or SIGPIPE on printf) would otherwise trip
# errexit at the ')' before rc is read, swallowing the diagnostic below.
set +e
(
	# An ignored disposition survives exec, so the worker sees EFBIG from the
	# write instead of dying on SIGXFSZ.
	trap '' XFSZ
	ulimit -f 1
	printf '@@notes#1|1785830400|10.0.0.99|limited\n%s\n@@\n' "$big" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
		--notes --dir="$work/var/notes" >/dev/null 2>>"$work/limited.log"
)
rc=$?
set -e
# Named, not swallowed: a worker that died before reaching the rename would
# leave the previous note in place and satisfy the assertion below for the
# wrong reason.
[ "$rc" -le 1 ] || fail "xymond_filestore exited $rc on a write it could not finish"

assert_equal "$orig" "$(cat "$work/var/notes/limited")" \
	"a write that could not complete replaced the previous note with a fragment"
[ ! -e "$work/var/notes/.limited" ] \
	|| fail "the incomplete temp file was left behind: $(ls -l "$work/var/notes/.limited")"

# The HTML page has the same contract, and the same shape of failure: it is
# renamed over the page the web server is currently serving.
setup
status_html "realhost"
htmlorig=$(cat "$work/var/html/realhost.conn.html")
[ -n "$htmlorig" ] || { cat "$work/worker.log" >&2; fail "the HTML page to be protected was not written"; }

set +e
(
	trap '' XFSZ
	ulimit -f 1
	printf '@@status#1|1785830400.000000|10.0.0.99|origin|realhost|conn|0|red|flags|red|1785830000|0||0||0|0\n%s\n@@\n' "$big" \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_filestore" --env="$work/etc/xymonserver.cfg" \
		--status --dir="$work/var/logs" --htmldir="$work/var/html" >/dev/null 2>>"$work/limited.log"
)
rc=$?
set -e
[ "$rc" -le 1 ] || fail "xymond_filestore exited $rc on an HTML write it could not finish"

assert_equal "$htmlorig" "$(cat "$work/var/html/realhost.conn.html")" \
	"an HTML page that could not be written whole replaced the served one with a fragment"
[ ! -e "$work/var/html/realhost.conn.html.tmp" ] \
	|| fail "the incomplete HTML temp file was left behind"

echo "OK $(basename "$0")"
