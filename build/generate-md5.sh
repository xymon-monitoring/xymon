#!/bin/sh

# cd ~/xymon/trunk

WEBLIST=`(cd xymond; find webfiles -type f) | egrep -v "RCS|\.svn" | xargs echo`
WWWLIST=`(cd xymond; find wwwfiles -type f) | egrep -v "RCS|\.svn" | xargs echo`

(cat build/md5.dat; for F in $WEBLIST $WWWLIST; do echo "md5:`openssl dgst -md5 xymond/${F} | awk '{print $2}'` ${F}"; done) | sort -k2 | uniq

