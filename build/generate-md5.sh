#!/bin/sh

cd ~/hobbit

VERLIST="beta/hobbit-4.0-beta4 beta/hobbit-4.0-beta5 beta/hobbit-4.0-beta6 beta/hobbit-4.0-RC1 beta/hobbit-4.0-RC2 beta/hobbit-4.0-RC3 beta/hobbit-4.0-RC4 beta/hobbit-4.0-RC5 beta/hobbit-4.0-RC6 release/hobbit-4.0 release/hobbit-4.0.1"

WEBLIST=`(cd ~/hobbit/hobbitd; find webfiles -type f) | grep -v RCS | xargs echo`
WWWLIST=`(cd ~/hobbit/hobbitd; find wwwfiles -type f) | grep -v RCS | xargs echo`

for F in $WEBLIST $WWWLIST
do
	for V in $VERLIST
	do
		if test -f $V/hobbitd/$F; then
			echo "`/usr/lib/hobbit/server/bin/bbdigest md5 $V/hobbitd/$F` $F"
		fi
	done
done | sort -k2 | uniq

