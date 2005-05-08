#!/bin/sh

cd ~/hobbit

WEBLIST=`(cd hobbitd; find webfiles -type f) | grep -v RCS | xargs echo`
WWWLIST=`(cd hobbitd; find wwwfiles -type f) | grep -v RCS | xargs echo`

(cat build/md5.dat; for F in $WEBLIST $WWWLIST; do echo "`/usr/lib/hobbit/server/bin/bbdigest md5 hobbitd/$F` $F"; done) | sort -k2 | uniq

