#!/bin/sh
#----------------------------------------------------------------------------#
# OpenBSD client for Xymon                                                   #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
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
df -k -tnonfs,kernfs,procfs,cd9660 | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[inode]"
df -i -tnonfs,kernfs,procfs,cd9660 | sed -e '/^[^       ][^     ]*$/{
N
s/[     ]*\n[   ]*/ /
}' | awk '
NR<2{printf "%-20s %10s %10s %10s %10s %s\n", $1, "itotal", $6, $7, $8, $9} 
NR>=2{printf "%-20s %10d %10d %10d %10s %s\n", $1, $6+$7, $6, $7, $8, $9}'
echo "[mount]"
mount
echo "[meminfo]"
$XYMONHOME/bin/openbsd-meminfo
echo "[swapctl]"
/sbin/swapctl -s
echo "[ifconfig]"
ifconfig -A
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ifstat]"
netstat -i -b -n | egrep -v "^lo|<Link"
echo "[ports]"
(netstat -na -f inet; netstat -na -f inet6) | grep "^tcp"
echo "[ps]"
ps -ax -ww -o pid,ppid,user,start,state,pri,pcpu,cputime,pmem,rss,vsz,args

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[nproc]"
        sysctl -n hw.ncpu
        echo "[top]"
	$TOP -n 20
    fi
fi

# vmstat
nohup sh -c "vmstat 300 2 1>$XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ $XYMONTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $XYMONTMP/xymon_vmstat.$MACHINEDOTS; rm -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; fi

exit

