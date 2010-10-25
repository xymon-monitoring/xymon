#!/bin/sh

# cd ~/hobbit

WEBLIST=`(cd xymond; find webfiles -type f) | egrep -v "RCS|\.svn" | xargs echo`
WWWLIST=`(cd xymond; find wwwfiles -type f) | egrep -v "RCS|\.svn" | xargs echo`

(cat build/md5.dat; for F in $WEBLIST $WWWLIST; do echo "`./common/xymondigest md5 xymond/$F` $F"; done) | sort -k2 | uniq

