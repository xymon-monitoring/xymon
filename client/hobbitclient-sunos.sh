#!/bin/sh
#----------------------------------------------------------------------------#
# Solaris client for Hobbit                                                  #
#                                                                            #
# Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-sunos.sh,v 1.18 2006-07-05 05:52:22 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who

echo "[df]"
# All of this because Solaris df cannot show multiple fs-types, or exclude certain fs types.
FSTYPES=`/bin/df -n -l|awk '{print $3}'|egrep -v "^proc|^fd|^mntfs|^ctfs|^devfs|^objfs|^nfs"|sort|uniq`
if test "$FSTYPES" = ""; then FSTYPES="ufs"; fi
set $FSTYPES
/bin/df -F $1 -k | grep -v " /var/run"
shift
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

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
        $TOP -b 20
    fi
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

