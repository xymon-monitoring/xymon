#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build/releasenotes.sh X.Y.Z -- at release, move the entries waiting in
# releasenotes.d/ into RELEASENOTES, and close its section for X.Y.Z
# (RELEASING.md, step 1).
# build/releasenotes.sh --check -- read the entries and change nothing.
#
# A pull request writes its RELEASENOTES entry as a file of its own,
# releasenotes.d/NAME.txt, so that two pull requests never write to the same
# place; releasenotes.d/README says how. This puts them where they are read:
#
# - under "Changes for X.Y.Z (not yet released)" when RELEASENOTES has that
#   section, above the entries already there; the heading loses its "(not
#   yet released)";
# - under a new "Changes for X.Y.Z" section otherwise, opened above the last
#   release's, and the title box is set to X.Y.Z.
#
# Entries go in the order of their file names, and their files are removed.
# With no entry and no open section there is nothing to say about X.Y.Z, and
# RELEASENOTES is left alone.
#
# Nothing is written unless everything can be: an entry not written as
# RELEASING.md asks -- a title "component -- what changes", dashes of its
# length, then at most two paragraphs of prose, no line over 80 columns,
# no pull-request number -- an open section naming another
# version, a section for X.Y.Z already closed while entries wait: each is
# refused first. Run from the top of the tree.

set -eu

DIR=releasenotes.d
NOTES=RELEASENOTES
OPEN=" (not yet released)"

usage() { echo "usage: $0 X.Y.Z | --check" >&2; exit 1; }
die() { echo "$0: $*" >&2; exit 1; }

VERSION="${1:-}"
[ "$#" -eq 1 ] || usage
if [ "$VERSION" != "--check" ]; then
	case "$VERSION" in
		*[!0-9.]* | .* | *. | *..* | "") usage ;;
	esac
	IFS=. read -r major minor patch rest <<EOT
$VERSION
EOT
	if [ -z "$major" ] || [ -z "$minor" ] || [ -z "$patch" ] || [ -n "$rest" ]; then
		usage
	fi
fi

[ -d "$DIR" ] || die "no $DIR directory here: run from the top of the tree"

# The entries, in the order of their names whatever the locale.
entries=$(LC_ALL=C; export LC_ALL; ls "$DIR" | grep '\.txt$' | sort) || entries=""

# Anything else in the directory is an entry that would never be released.
strays=$(ls -A "$DIR" | grep -v '\.txt$' | grep -vx 'README') || strays=""
[ -z "$strays" ] || die "$DIR holds something that is neither README nor an entry named NAME.txt: $(echo $strays)"

# check_entry FILE -- a title, dashes of its length, prose within the bounds
# RELEASING.md sets; nothing that RELEASENOTES would read as a section of
# its own.
check_entry() {
	awk -v file="$1" -v maxcols=80 -v maxparas=2 '
		function bad(why) { printf "%s: %s\n", file, why; failed = 1 }
		NR == 1 {
			title = $0
			if ($0 !~ /^[^ \t-][^ \t]*( [^ \t]+)* -- [^ \t]/) bad("line 1 is not a title, \"component -- what changes\"")
		}
		NR == 2 {
			if ($0 !~ /^-+$/) bad("line 2 is not a line of dashes under the title")
			else if (length($0) != length(title)) bad("the dashes are " length($0) " long, the title " length(title))
		}
		NR > 2 && $0 != "" { prose++; if (!inpara) { paras++; inpara = 1 } }
		NR > 2 && $0 == "" { inpara = 0 }
		length($0) > maxcols && NR != 2 { bad("line " NR " is " length($0) " columns wide, over " maxcols) }
		/#[0-9]+/ { bad("line " NR " names a pull request or an issue by its number") }
		/^Changes for / { bad("line " NR " begins a section, \"Changes for\"") }
		/^=+$/ { bad("line " NR " underlines a section") }
		/[ \t]$/ { bad("line " NR " ends with a blank") }
		/^(<<<<<<<|>>>>>>>)( |$)/ { bad("line " NR " is a merge conflict marker") }
		{ last = $0 }
		END {
			if (NR < 3 || !prose) bad("no prose under the title")
			else if (last == "") bad("ends with an empty line")
			if (paras > maxparas) bad(paras " paragraphs, over " maxparas)
			exit failed
		}' "$1"
}

failed=0
for e in $entries; do
	[ -f "$DIR/$e" ] || die "$DIR/$e is not a file"
	[ -z "$(tail -c 1 "$DIR/$e")" ] || { echo "$DIR/$e: the last line has no newline"; failed=1; }
	check_entry "$DIR/$e" || failed=1
done
[ "$failed" -eq 0 ] || die "an entry of $DIR is not written as $DIR/README says"

[ "$VERSION" != "--check" ] || exit 0

[ -f "$NOTES" ] || die "no $NOTES here: run from the top of the tree"
open=$(grep -n '^Changes for .*(not yet released)$' "$NOTES") || open=""
case "$open" in
	*"
"*)	die "$NOTES has more than one section not yet released" ;;
esac
if [ -n "$open" ] && [ "${open#*:}" != "Changes for $VERSION$OPEN" ]; then
	die "$NOTES has \"${open#*:}\" open, not $VERSION: rename it first (RELEASING.md)"
fi
if [ -z "$open" ] && grep -qxF "Changes for $VERSION" "$NOTES"; then
	[ -z "$entries" ] || die "$NOTES has the section for $VERSION closed already, and $DIR still holds entries"
	echo "$NOTES: the section for $VERSION is closed already"
	exit 0
fi
if [ -z "$open" ] && [ -z "$entries" ]; then
	echo "$NOTES: no entry for $VERSION, left alone"
	exit 0
fi
if [ -z "$open" ]; then
	grep -q '^Changes for ' "$NOTES" || die "$NOTES has no \"Changes for\" section to open one above"
	grep -q '^ *\* \* \*   Release notes for Xymon .*   \* \* \*$' "$NOTES" || die "$NOTES has no title box to set to $VERSION"
fi

# The entries, each followed by an empty line.
: >"$NOTES.entries"
for e in $entries; do
	cat "$DIR/$e" >>"$NOTES.entries"
	echo >>"$NOTES.entries"
done

awk -v version="$VERSION" -v open="${open#*:}" -v entries="$NOTES.entries" '
	function underline(s,   u) { u = s; gsub(/./, "=", u); return u }
	function put_entries(   l) { while ((getline l < entries) > 0) print l; close(entries) }
	# The section is open: its heading closes, and the entries go under it.
	open != "" && $0 == open {
		print "Changes for " version
		print underline("Changes for " version)
		getline			# its underline
		print ""
		put_entries()
		# The empty line that followed the underline, when there was one.
		if ((getline) > 0 && $0 != "") print
		next
	}
	# No section yet: the title box names the version, and the section
	# opens above the last release.
	open == "" && !boxed && /^ *\* \* \*   Release notes for Xymon .*   \* \* \*$/ {
		sub(/Release notes for Xymon .*   \* \* \*$/, "Release notes for Xymon " version "   * * *")
		boxed = 1
	}
	open == "" && !placed && /^Changes for / {
		print "Changes for " version
		print underline("Changes for " version)
		print ""
		put_entries()
		placed = 1
	}
	{ print }
' "$NOTES" >"$NOTES.new"
rm -f "$NOTES.entries"
mv "$NOTES.new" "$NOTES"

n=0
for e in $entries; do
	rm -f "$DIR/$e"
	n=$((n + 1))
done
echo "$NOTES: section for $VERSION closed, $n entr$([ "$n" -eq 1 ] && echo y || echo ies) moved in from $DIR"
