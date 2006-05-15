#!/bin/sh
#
#----------------------------------------------------------------------------#
# HP-UX client for Hobbit                                                    #
#                                                                            #
# Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-hp-ux.sh,v 1.17 2006-05-15 13:29:02 henrik Exp $

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
df -Pk | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[memory]"
$BBHOME/bin/hpux-meminfo
echo "[swapinfo]"
/usr/sbin/swapinfo -tm
echo "[ifconfig]"
netstat -in
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ifstat]"
/usr/sbin/lanscan -p | while read PPA; do /usr/sbin/lanadmin -g mibstats $PPA; done
echo "[ports]"
netstat -an | grep "^tcp"
echo "[ps]"
UNIX95=1 ps -Ax -o pid,ppid,user,stime,state,pri,pcpu,time,vsz,args
echo "[top]"
# Cits Bogajewski 03-08-2005: redirect of top fails
top -d 1 -f $BBHOME/tmp/top.OUT
cat $BBHOME/tmp/top.OUT
rm $BBHOME/tmp/top.OUT
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ $BBTMP/hobbit_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat.$MACHINEDOTS; rm -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; fi
# logfiles
if test -f $LOGFETCHCFG
then
    $BBHOME/bin/logfetch $LOGFETCHCFG $LOGFETCHSTATUS
fi

exit

