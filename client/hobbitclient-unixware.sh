#!/bin/sh
#----------------------------------------------------------------------------#
# SCO Unixware client for Xymon                                              #
#                                                                            #
# Copyright (C) 2005-2010 Henrik Storner <henrik@hswn.dk>                    #
# Copyright (C) 2006 Charles Goyard <cg@fsck.fr>                             #
# Copyright (C) 2009 Buchan Milne <bgmilne@mandriva.org>                     #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $$

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who -x
echo "[df]"
df -P
echo "[mount]"
mount -v
echo "[memsize]"
/etc/memsize
echo "[freemem]"
sar -r 1 2 | tail -1
echo "[swap]"
swap -l
echo "[ifconfig]"
ifconfig -a
#echo "[ifstat]"
#ifconfig -in
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ports]"
netstat -an | grep "^tcp"
echo "[ps]"
#ps -A -o pid,ppid,user,stime,s,pri,pcpu,time,vsz,args
ps -A -o pid,ppid,user,pcpu,time,vsz,args
# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
	$TOP -b -n 1 
    fi
fi
# vmstat
#nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ $BBTMP/hobbit_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
#sleep 5
#if test -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat.$MACHINEDOTS; rm -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; fi

exit

