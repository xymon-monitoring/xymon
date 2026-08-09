#!/bin/bash
set -euo pipefail

# Pin collation so md5.dat sort order doesn't depend on the caller's locale.
export LC_ALL=C

# cd ~/xymon/trunk

WEBLIST=$( (cd xymond && find webfiles -type f) | grep -Ev "RCS|\.svn" )
WWWLIST=$( (cd xymond && find wwwfiles -type f) | grep -Ev "RCS|\.svn" )

# md5.dat must keep the hashes of every previously shipped version:
# setup-newfiles only overwrites an installed file when its hash matches
# one of these, so dropping old entries would break upgrade detection.
{
	cat build/md5.dat
	for F in $WEBLIST $WWWLIST; do
		H=$(openssl dgst -md5 "xymond/$F" | awk '{print $2}')
		if [ -z "$H" ]; then
			echo "md5 digest failed for xymond/$F" >&2
			exit 1
		fi
		echo "md5:$H $F"
	done
} | sort -k2 | uniq
