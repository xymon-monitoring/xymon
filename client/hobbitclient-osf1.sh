#!/bin/sh
#----------------------------------------------------------------------------#
# OSF1 client for Hobbit                                                     #
#                                                                            #
# Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-osf1.sh,v 1.9 2006-05-14 20:08:48 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[memory]"
vmstat -P
echo "[swap]"
swapon -s
echo "[df]"
df -t noprocfs | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[ifconfig]"
ifconfig -a
echo "[route]"
cat /etc/routes
echo "[netstat]"
netstat -s
echo "[ports]"
netstat -an|grep "^tcp"
echo "[ps]"
ps -ef
echo "[top]"
top -b -n 1 
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ $BBTMP/hobbit_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat.$MACHINEDOTS; rm -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; fi
# logfiles
$BBHOME/bin/logfetch $LOGFETCHCFG $LOGFETCHSTATUS

exit

