#!/bin/bash
#
# Generate docs/manpages/manN/*.html from the manual page sources.
#
#   build/makehtml.sh VERSION                       regenerate every page
#   build/makehtml.sh VERSION common/hosts.cfg.5    regenerate one page
#
# The second form exists so that editing one manual page does not mean
# rewriting all 69 HTML files, which buries the real change in the diff.
set -euo pipefail

# LC_ALL, not LANG: LANG=C is overridden by any LC_* the caller has set.
export LC_ALL=C
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
	DATE=$(date -u -d "@$SOURCE_DATE_EPOCH" +"%e %b %Y" 2>/dev/null) \
		|| DATE=$(date -u -r "$SOURCE_DATE_EPOCH" +"%e %b %Y")
else
	DATE=$(date +"%e %b %Y")
fi
VERSION="${1:-}"
if [ "$VERSION" = "" ]
then
	echo "Usage: $0 VERSION [manpage-source ...]"
	echo "       $0 4.3.31                      # every page"
	echo "       $0 4.3.31 common/hosts.cfg.5   # just this one"
	exit 1
fi
shift

POST="`dirname "$0"`/manpage-html.py"
if [ ! -x "$POST" ]
then
	echo "$0: $POST is missing or not executable" >&2
	exit 1
fi
# The post-processor is Python, and being executable does not mean its
# interpreter is installed: without one the shebang fails inside the pipeline,
# after the output file has already been truncated, leaving a 0-byte page and
# a bare "command not found". Refuse first, the way mandoc is refused.
"$POST" --selftest </dev/null >/dev/null 2>&1 || {
	echo "$0: cannot run $POST - is python3 installed?" >&2
	exit 1
}
command -v mandoc >/dev/null 2>&1 || {
	echo "$0: mandoc is not installed (Debian/Ubuntu: mandoc, RHEL: mandoc)" >&2
	exit 1
}

# The post-processor only links references to pages this tree actually ships,
# so it needs the list. Built once, from the same directories the walk uses.
KNOWN=`for d in xymongen xymonnet xymonproxy common xymond web; do \
	for s in 1 5 7 8; do \
		for f in $d/*.$s; do [ -r "$f" ] && basename "$f"; done; \
	done; \
done 2>/dev/null | sed -e 's/$/.html/' | sort -u | tr '\n' ' '`

# Convert one source file. The section comes from the filename, so this works
# whether we were handed a path or found it by walking the source directories.
onepage()
{
	FILE="$1"

	if [ ! -r "$FILE" ]
	then
		echo "$0: cannot read $FILE" >&2
		return 1
	fi

	SECT=`echo "$FILE" | sed -e 's/.*\.//'`
	case "$SECT" in
		1|5|7|8) ;;
		*) echo "$0: $FILE is not a manual page in section 1, 5, 7 or 8" >&2
		   return 1 ;;
	esac

	NAME=`head -n 1 "$FILE" | awk '{print $2}'`
	SECTION=`head -n 1 "$FILE" | awk '{print $3}'`
	if [ "$NAME" = "" -o "$SECTION" = "" ]
	then
		echo "$0: $FILE has no usable .TH line" >&2
		return 1
	fi

	mkdir -p "docs/manpages/man$SECT"
	(echo ".TH $NAME $SECTION \"Version $VERSION: $DATE\" \"Xymon\""; tail -n +2 "$FILE") | \
	mandoc -T html -O style=../mandoc.css | \
	"$POST" $KNOWN >"docs/manpages/man$SECT/`basename $FILE`.html"
}

rc=0

if [ $# -gt 0 ]
then
	# Named pages only: leave the rest of docs/manpages alone.
	for FILE in "$@"
	do
		onepage "$FILE" || rc=1
	done
else
	# Everything. Clear the output first, so that a page removed from the
	# sources does not leave its HTML behind. Only the output: the list used
	# to start with docs/manpages/index.html, which this script has never
	# written - it is the hand-kept docs/man-index.html, put there by
	# docs/Makefile at install time.
	rm -f docs/*~ \
	      docs/manpages/man1/* docs/manpages/man5/* \
	      docs/manpages/man7/* docs/manpages/man8/*

	for DIR in xymongen xymonnet xymonproxy common xymond web
	do
		for SECT in 1 5 7 8
		do
			for FILE in $DIR/*.$SECT
			do
				if [ -r "$FILE" ]
				then
					onepage "$FILE" || rc=1
				fi
			done
		done
	done
fi

exit $rc

# Sourceforge update
# cd ~/xymon/trunk/docs && rsync -av --rsh=ssh --exclude=RCS ./ storner@shell.sourceforge.net:/home/groups/x/xy/xymon/htdocs/docs/
