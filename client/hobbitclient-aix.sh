#!/bin/sh
#----------------------------------------------------------------------------#
# AIX client for Hobbit                                                      #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-aix.sh,v 1.9 2006-04-23 12:13:37 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
# The sed stuff is to make sure lines are not split into two.
df -Ik | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[realmem]"
lsattr -El sys0 -a realmem
echo "[freemem]"
vmstat 1 2 | tail -1
echo "[swap]"
lsps -s
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -r
echo "[netstat]"
netstat -s
echo "[ifstat]"
netstat -v
echo "[ports]"
netstat -an | grep "^tcp"
echo "[ifstat]"
netstat -v
echo "[ps]"
ps axuww
echo "[top]"
top -b 20
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi
# logfiles
$BBHOME/bin/logfetch $BBTMP/logfetch.cfg $BBTMP/logfetch.status

exit

