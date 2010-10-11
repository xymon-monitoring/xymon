#!/bin/sh

# cd ~/hobbit

WEBLIST=`(cd hobbitd; find webfiles -type f) | egrep -v "RCS|\.svn" | xargs echo`
WWWLIST=`(cd hobbitd; find wwwfiles -type f) | egrep -v "RCS|\.svn" | xargs echo`

(cat build/md5.dat; for F in $WEBLIST $WWWLIST; do echo "`./common/bbdigest md5 hobbitd/$F` $F"; done) | sort -k2 | uniq

