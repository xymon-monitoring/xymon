#!/bin/sh

cd ~/hobbit/beta

VERLIST="4.0-beta4 4.0-beta5 4.0-beta6 4.0-RC1 4.0-RC2 4.0-RC3 4.0-RC4 4.0-RC5 4.0-RC6"

WEBLIST=`(cd ~/hobbit/hobbitd; find webfiles -type f) | grep -v RCS | xargs echo`
WWWLIST=`(cd ~/hobbit/hobbitd; find wwwfiles -type f) | grep -v RCS | xargs echo`

for F in $WEBLIST $WWWLIST
do
	for V in $VERLIST
	do
		if test -f hobbit-$V/hobbitd/$F; then
			echo "`../common/bbdigest md5 hobbit-$V/hobbitd/$F` $F"
		fi
	done
done | sort -k2 | uniq

