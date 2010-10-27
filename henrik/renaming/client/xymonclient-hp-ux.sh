#!/bin/sh
#
#----------------------------------------------------------------------------#
# HP-UX client for Xymon                                                     #
#                                                                            #
# Copyright (C) 2005-2010 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

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
echo "[mount]"
mount
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

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
	# Cits Bogajewski 03-08-2005: redirect of top fails
	$TOP -d 1 -f $BBHOME/tmp/top.OUT
	cat $BBHOME/tmp/top.OUT
	rm $BBHOME/tmp/top.OUT
    fi
fi

# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/xymon_vmstat.$MACHINEDOTS.$$ $BBTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $BBTMP/xymon_vmstat.$MACHINEDOTS; rm -f $BBTMP/xymon_vmstat.$MACHINEDOTS; fi

exit

