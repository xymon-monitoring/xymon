#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-save.sh
#
# The critical systems editor says what each change did. Runs the real
# criticaleditor.cgi against a scratch critical.cfg:
#
#   - a save, a clone change and a delete say "Saved" or "Deleted", and did it;
#   - a critical.cfg that can't be opened says "NOT SAVED" and why, and is
#     left as it was;
#   - a critical.cfg.bak that can't be written says the change was saved,
#     but the backup was not;
#   - a write that fails part way (a full disk, here the file size limit)
#     says "NOT SAVED", and critical.cfg is put back from critical.cfg.bak
#     instead of being left cut short;
#   - Drop through a clone, on a test with no record, or on a record other
#     hosts are clones of, deletes nothing and doesn't say "Deleted";
#   - the result is shown together with the note about clones, not instead;
#   - what the user typed is escaped;
#   - a critical.cfg that is there but cannot be read is not written: it
#     cannot be backed up, and nothing of it was loaded. Root reads any file,
#     so this case runs on an editor built with
#     criticaleditor-save-harness.c, whose fopen() refuses to read a name
#     holding "unreadable".

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"

pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/criticaleditor" "$ROOT/web/criticaleditor.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "criticaleditor does not compile: $(cat "$work/cc.log")"

mkdir -p "$work/web" "$work/etc"
cp "$ROOT"/xymond/webfiles/critedit_* "$work/web/"
printf '127.0.0.1 alpha.example.com # \n127.0.0.1 beta.example.com # \n' >"$work/etc/hosts.cfg"
CFG=$work/etc/critical.cfg

EDITOR=$work/criticaleditor
edit() {	# QUERY_STRING [config file]
	rc=0
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="$1" XYMONHOME="$work" \
		HOSTSCFG="!$work/etc/hosts.cfg" \
		"$EDITOR" --config="${2:-$CFG}" 2>/dev/null) || rc=$?
	[ "$rc" -eq 0 ] || fail "criticaleditor failed (rc $rc) for '$1'"
	# The line above the form
	MSG=$(sed -n 's/.*<big>\(.*\)<\/big>.*/\1/p' <<<"$OUT")
}

: >"$CFG"

# A save, a clone added, a clone dropped, a delete
edit "HOSTNAME=alpha.example.com&SERVICE=cpu&PRIORITY=1&Update=1"
[ "$MSG" = "Saved" ] || fail "a save said '$MSG', not Saved"
grep -q '^alpha.example.com|cpu|' "$CFG" || fail "the save did not write the record: $(cat "$CFG")"

edit "HOSTNAME=alpha.example.com&SERVICE=cpu&CRITEDITADDCLONES=beta.example.com&Clone=1"
[ "$MSG" = "Saved" ] || fail "adding a clone said '$MSG', not Saved"
grep -q '^beta.example.com|=alpha.example.com' "$CFG" || fail "the clone was not written: $(cat "$CFG")"

# Drop through the clone: beta has no record of its own
before=$(cat "$CFG")
edit "HOSTNAME=beta.example.com&SERVICE=cpu&Drop=1"
assert_contains "Nothing deleted: beta.example.com is a clone of alpha.example.com" "$MSG" "Drop through a clone did not say so"
assert_not_contains "Deleted" "$MSG" "Drop through a clone said Deleted"
[ "$(cat "$CFG")" = "$before" ] || fail "Drop through a clone changed critical.cfg"

# Drop on the record beta is a clone of, without "even if it has clones"
edit "HOSTNAME=alpha.example.com&SERVICE=cpu&Drop=1"
assert_contains "Will not delete record that is cloned" "$MSG" "Drop on a cloned record did not say why not"
assert_not_contains "Deleted" "$MSG" "Drop on a cloned record said Deleted"
[ "$(cat "$CFG")" = "$before" ] || fail "Drop on a cloned record changed critical.cfg"

# Drop on a test with no record
edit "HOSTNAME=alpha.example.com&SERVICE=nosuch&Drop=1"
assert_contains "Nothing deleted: there is no record for this host and test" "$MSG" "Drop on a missing record did not say so"
[ "$(cat "$CFG")" = "$before" ] || fail "Drop on a missing record changed critical.cfg"

edit "HOSTNAME=alpha.example.com&SERVICE=cpu&CRITEDITCLONELIST=beta.example.com&Clone=1"
[ "$MSG" = "Saved" ] || fail "dropping a clone said '$MSG', not Saved"
grep -q '^beta.example.com|=' "$CFG" && fail "the clone was not dropped: $(cat "$CFG")"

edit "HOSTNAME=alpha.example.com&SERVICE=cpu&Drop=1"
[ "$MSG" = "Deleted" ] || fail "a delete said '$MSG', not Deleted"
grep -q '^alpha.example.com|cpu|' "$CFG" && fail "the deleted record is still there: $(cat "$CFG")"

# The result and the note about clones, both
edit "HOSTNAME=alpha.example.com&SERVICE=cpu&PRIORITY=1&Update=1"
edit "HOSTNAME=alpha.example.com&SERVICE=cpu&CRITEDITADDCLONES=beta.example.com&Clone=1"
edit "HOSTNAME=alpha.example.com&SERVICE=cpu&DROPEVENIFCLONED=on&Drop=1"
assert_contains "Deleted<BR>Warning: Orphans will be ignored" "$MSG" "the result replaced the note about clones"
edit "HOSTNAME=beta.example.com&SERVICE=cpu&CRITEDITCLONELIST=beta.example.com&Clone=1"

# Not both a host and a test
edit "HOSTNAME=alpha.example.com&PRIORITY=1&Update=1"
assert_contains "NOT SAVED: enter both a host and a test" "$MSG" "a record without a test was not refused"

# critical.cfg can't be opened: a path that can't be created, whoever runs the test
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1" "$work/nodir/critical.cfg"
assert_contains "NOT SAVED: cannot write critical.cfg:" "$MSG" "a failed open did not say so"
assert_contains "which must be able to write critical.cfg and critical.cfg.bak" "$MSG" "a failed open did not say what is needed"

# critical.cfg.bak can't be written: it is a directory
rm -f "$CFG.bak"; mkdir "$CFG.bak"
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1"
assert_contains "Saved, but critical.cfg.bak could not be written:" "$MSG" "a failed backup was not reported"
grep -q '^alpha.example.com|conn|' "$CFG" || fail "the save with a failed backup was not written"
rmdir "$CFG.bak"

# A write that fails part way: a file size limit below the new critical.cfg,
# above the old one, so the backup is made and the write is not. One block is
# 512 or 1024 bytes depending on the shell; the old file stays under 512, the
# new one is well over 1024. SIGXFSZ is ignored, so the write fails with EFBIG.
long=$(printf 'x%.0s' $(seq 1 3000))
cp "$CFG" "$work/before.cfg"
[ "$(wc -c <"$CFG")" -lt 512 ] || fail "the test's critical.cfg is past the file size limit before the case"
failwrite() {	# service
	rc=0
	OUT=$( (trap '' XFSZ; ulimit -f 1; REQUEST_METHOD=GET \
		QUERY_STRING="HOSTNAME=alpha.example.com&SERVICE=$1&PRIORITY=1&EXTRA=$long&Update=1" \
		XYMONHOME="$work" HOSTSCFG="!$work/etc/hosts.cfg" \
		exec "$work/criticaleditor" --config="$CFG") 2>/dev/null) || rc=$?
	[ "$rc" -eq 0 ] || fail "criticaleditor failed (rc $rc) on a failing write"
	MSG=$(sed -n 's/.*<big>\(.*\)<\/big>.*/\1/p' <<<"$OUT")
	assert_contains "NOT SAVED: writing critical.cfg failed:" "$MSG" "a failed write was reported as '$MSG'"
}
failwrite disk
assert_contains "It was put back as it was before this change" "$MSG" "a failed write did not say critical.cfg was put back"
cmp -s "$CFG" "$work/before.cfg" || fail "after a failed write critical.cfg is not byte for byte as it was"

# The same with no critical.cfg to back up, and a critical.cfg.bak left from
# before: that old backup is not the file this change replaced, so it is not
# put back, and the editor does not say it was.
mv "$CFG" "$work/kept.cfg"
printf 'old.example.com|cpu||||1|||\n' >"$CFG.bak"
failwrite disk
assert_contains "it could not be put back" "$MSG" "a failed write with only a stale backup was reported as '$MSG'"
assert_not_contains "old.example.com" "$(cat "$CFG")" "a stale critical.cfg.bak was put back"
mv "$work/kept.cfg" "$CFG"

# What the user typed is escaped: a clone named with markup, then Drop on it
edit "HOSTNAME=alpha.example.com&SERVICE=conn&CRITEDITADDCLONES=%3Cb%3Ex%3C%2Fb%3E&Clone=1"
edit "HOSTNAME=%3Cb%3Ex%3C%2Fb%3E&SERVICE=conn&Drop=1"
assert_contains "Nothing deleted: &lt;b&gt;x&lt;/b&gt; is a clone of" "$MSG" "a typed host name was not escaped in the message"
assert_not_contains "<b>x</b>" "$OUT" "a typed host name reached the page unescaped"

# critical.cfg is there but cannot be read. The editor is built a second time
# with the harness's fopen(); a probe checks first that it is the one the
# editor calls: where it is, a save to a name holding "unwritable" fails.
"$CC" $harness_cflags -o "$work/criticaleditor-harness" "$ROOT/web/criticaleditor.c" \
	"$ROOT/tests/web/criticaleditor-save-harness.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "criticaleditor does not compile with the harness: $(cat "$work/cc.log")"
EDITOR=$work/criticaleditor-harness
mkdir -p "$work/unwritable" "$work/unreadable"
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1" "$work/unwritable/critical.cfg"
case $MSG in
	"NOT SAVED: cannot write critical.cfg:"*) ;;
	*) pass_partial "the critical systems editor says whether each change was saved or deleted, and why not" \
		"the refusing fopen() is not the one the editor calls here, so an unreadable critical.cfg is not checked" ;;
esac

RCFG=$work/unreadable/critical.cfg
printf 'alpha.example.com|cpu||||1|||\nbeta.example.com|=alpha.example.com\n' >"$RCFG"
cp "$RCFG" "$work/unreadable-before.cfg"
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1" "$RCFG"
assert_contains "NOT SAVED: cannot read critical.cfg: Permission denied" "$MSG" "a save over an unreadable critical.cfg was reported as '$MSG'"
assert_contains "which must be able to read and write critical.cfg" "$MSG" "an unreadable critical.cfg did not say what is needed"
cmp -s "$RCFG" "$work/unreadable-before.cfg" || fail "a save over an unreadable critical.cfg changed it"
[ ! -e "$RCFG.bak" ] || fail "a save over an unreadable critical.cfg wrote a backup"

# Not there at all is a first save, not an unreadable file
mkdir -p "$work/first"
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1" "$work/first/critical.cfg"
[ "$MSG" = "Saved" ] || fail "a first save, with no critical.cfg yet, said '$MSG', not Saved"

pass "the critical systems editor says whether each change was saved or deleted, and why not"
