#!/bin/sh
#----------------------------------------------------------------------------#
# Solaris client for Hobbit                                                  #
#                                                                            #
# Copyright (C) 2005-2009 Henrik Storner <henrik@hswn.dk>                    #
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
# Bothersome, because Solaris df cannot show multiple fs-types, or exclude certain fs types.
# Print the root filesystem first, with the header, and those fs's that have the same type.
ROOTFSTYPE=`/bin/df -n / | awk '{print $3}'`
/bin/df -F $ROOTFSTYPE -k
# Then see what fs types are in use, and weed out those we dont want.
FSTYPES=`/bin/df -n -l|cut -d: -f2 | awk '{print $1}'|egrep -v "^${ROOTFSTYPE}|^proc|^fd|^mntfs|^ctfs|^devfs|^objfs|^nfs|^lofs"|sort|uniq`
set $FSTYPES
while test "$1" != ""; do
  /bin/df -F $1 -k | grep -v " /var/run" | tail +2
  shift
done

echo "[mount]"
mount
echo "[prtconf]"
/usr/sbin/prtconf
echo "[memory]"
vmstat 1 2 | tail -1
echo "[swap]"
/usr/sbin/swap -s
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ports]"
netstat -na -f inet -P tcp | tail +3
netstat -na -f inet6 -P tcp | tail +5
echo "[ifstat]"
/usr/bin/kstat -p -s '[or]bytes64' | sort
echo "[ps]"
ps -A -o pid,ppid,user,stime,s,pri,pcpu,time,pmem,rss,vsz,args

# If TOP is defined, then use it. If not, fall back to the Solaris prstat command.
echo "[top]"
if test "$TOP" != "" -a -x "$TOP"
then
	"$TOP" -b 20
else
	prstat -can 20 1 1
fi

# vmstat and iostat (iostat -d provides a cpu utilisation with I/O wait number)
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$MACHINEDOTS.$$ $BBTMP/hobbit_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
nohup sh -c "iostat -c 300 2 1>$BBTMP/hobbit_iostatcpu.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_iostatcpu.$MACHINEDOTS.$$ $BBTMP/hobbit_iostatcpu.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
nohup sh -c "iostat -dxsrP 300 2 1>$BBTMP/hobbit_iostatdisk.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_iostatdisk.$MACHINEDOTS.$$ $BBTMP/hobbit_iostatdisk.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat.$MACHINEDOTS; rm -f $BBTMP/hobbit_vmstat.$MACHINEDOTS; fi
if test -f $BBTMP/hobbit_iostatcpu.$MACHINEDOTS; then echo "[iostatcpu]"; cat $BBTMP/hobbit_iostatcpu.$MACHINEDOTS; rm -f $BBTMP/hobbit_iostatcpu.$MACHINEDOTS; fi
if test -f $BBTMP/hobbit_iostatdisk.$MACHINEDOTS; then echo "[iostatdisk]"; cat $BBTMP/hobbit_iostatdisk.$MACHINEDOTS; rm -f $BBTMP/hobbit_iostatdisk.$MACHINEDOTS; fi

exit

