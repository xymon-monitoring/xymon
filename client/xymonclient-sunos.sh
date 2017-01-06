#!/bin/sh
#----------------------------------------------------------------------------#
# Solaris client for Xymon                                                   #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

# Work out what type of environment we are on 
#
# this will return the zone name on any non-global solaris zone
# or the word "Global" on any global solaris zone
# or the word "global" on any standalone solaris platform, including LDOMS
(/usr/sbin/zoneadm list 2>/dev/null || echo 1) | wc -l | grep '\<1\>' > /dev/null 2>&1 && ZTYPE=`/bin/zonename 2>/dev/null || echo global` || ZTYPE='Global'

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
# Then see what fs types are in use, and weed out those we don't want.
case $ZTYPE in
global|Global)
   FSTYPES=`/bin/df -n -l|cut -d: -f2 | awk '{print $1}'|egrep -v "^${ROOTFSTYPE}|^proc|^fd|^mntfs|^ctfs|^devfs|^objfs|^nfs|^lofs|^tmpfs|^sharefs"|sort|uniq`
   ;;
*)
   # zones are frequently type lofs so deal with them specially
   FSTYPES=`/bin/df -n -l|cut -d: -f2 | awk '{print $1}'|egrep -v "^${ROOTFSTYPE}|^proc|^fd|^mntfs|^ctfs|^devfs|^objfs|^nfs|^tmpfs|^sharefs"|sort|uniq`
   ;;
esac
# $FSTYPES may be empty
for fst in $FSTYPES; do
  case $ZTYPE in
    global|Global)
      /bin/df -F $fst -k | grep -v " /var/run" | tail +2
      ;;
    *)
      /bin/df -F $fst -k | egrep -v "( /var/run|^(/dev|/platform/))" | tail +2
      ;;
  esac
done

# This only works for ufs filesystems
echo "[inode]"
(if test -x /usr/ucb/df
then
   /usr/ucb/df -i
else
   df -o i 2>/dev/null
fi) | awk '
NR<2{printf "%-20s %10s %10s %10s %10s %s\n", $1, "itotal", $2, $3, $4, $5}
NR>=2{printf "%-20s %10d %10d %10d %10s %s\n", $1, $2+$3, $2, $3, $4, $5}'

echo "[mount]"
mount
echo "[prtconf]"
case $ZTYPE in
global|Global)
   /usr/sbin/prtconf 2>&1
   ;;
*)
   # provided  by the firmware (PROM) since devinfo facility not available
   # in a zone
   /usr/sbin/prtconf -p 2>&1
   ;;
esac
echo "[memory]"
case $ZTYPE in
global|Global)
   vmstat 1 2 | tail -1
   ;;
*)
   # need to modify vmstat output in a non-global zone
   VMSTAT=`vmstat 1 2 | tail -1`
   # cut out the useable parts
   VMFR=`echo $VMSTAT | cut -d " " -f1,2,3`
   VMBK=`echo $VMSTAT | cut -d " " -f6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22`
   # get the total memory for the platform
   TMEM=`/usr/sbin/prtconf 2>&1 | grep Memory | cut -d" " -f3`
   TMEM=`expr $TMEM "*" 1024`
   # get the total allocated and reserved swap
   SUSED=`swap -s | cut -d "=" -f2 | cut -d "," -f1 | cut -d " " -f2 | cut -d "k" -f1`
   # work out the RSS for this zone
   TUSED=`prstat -Z -n 1,1 1 1 | \
     nawk ' NR == 4 {
       do {
         if ($4 ~ /G$/) { sub(/G$/,"",$4); print $4*1024*1024; break; }
         if ($4 ~ /M$/) { sub(/M$/,"",$4); print $4*1024; break; }
         if ($4 ~ /K$/) { sub(/K$/,"",$4); print $4; break; }
         print "unknown"
       } while (0)}'`
   TUSED=`expr $TMEM "-" $TUSED`
   echo "$VMFR $SUSED $TUSED $VMBK"
   ;;
esac
echo "[swap]"
/usr/sbin/swap -s
# don't report the swaplist in a non-global zone because it will cause the
# server client module to miscalculate memory
case $ZTYPE in
global|Global)
   echo "[swaplist]"
   /usr/sbin/swap -l 2>/dev/null
   ;;
esac
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
# Leave out the wrmsd and mac interfaces. See http://www.xymon.com/archive/2009/06/msg00204.html
case $ZTYPE in
global|Global)
   /usr/bin/kstat -p -s '[or]bytes64' | egrep -v 'wrsmd|mac' | sort
   ;;
*)
   # find out which nics are configured into this zone
   MYNICS=`netstat -i |cut -f1 -d" " | egrep -i -v "Name|lo|^$"  |paste -s -d"|" - `
   /usr/bin/kstat -p -s '[or]bytes64' | egrep "$MYNICS"| sort
   ;;
esac
echo "[ps]"
case $ZTYPE in
global)
    /bin/ps -A -o pid,ppid,user,stime,s,pri,pcpu,time,pmem,rss,vsz,args
   ;;
Global)
    /bin/ps -A -o zone,pid,ppid,user,stime,class,s,pri,pcpu,time,pmem,rss,vsz,args | sort
   ;;
*)
    /bin/ps -A -o pid,ppid,user,stime,class,s,pri,pcpu,time,pmem,rss,vsz,args
   ;;
esac

# If TOP is defined, then use it. If not, fall back to the Solaris prstat command.
echo "[top]"
if test "$TOP" != "" -a -x "$TOP"
then
	"$TOP" -b 20
else
	prstat -can 20 1 1
fi

# vmstat and iostat (iostat -d provides a cpu utilisation with I/O wait number)
nohup sh -c "vmstat 300 2 1>$XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ $XYMONTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
nohup sh -c "iostat -c 300 2 1>$XYMONTMP/xymon_iostatcpu.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_iostatcpu.$MACHINEDOTS.$$ $XYMONTMP/xymon_iostatcpu.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
nohup sh -c "iostat -dxsrP 300 2 1>$XYMONTMP/xymon_iostatdisk.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_iostatdisk.$MACHINEDOTS.$$ $XYMONTMP/xymon_iostatdisk.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $XYMONTMP/xymon_vmstat.$MACHINEDOTS; rm -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; fi
if test -f $XYMONTMP/xymon_iostatcpu.$MACHINEDOTS; then echo "[iostatcpu]"; cat $XYMONTMP/xymon_iostatcpu.$MACHINEDOTS; rm -f $XYMONTMP/xymon_iostatcpu.$MACHINEDOTS; fi
if test -f $XYMONTMP/xymon_iostatdisk.$MACHINEDOTS; then echo "[iostatdisk]"; cat $XYMONTMP/xymon_iostatdisk.$MACHINEDOTS; rm -f $XYMONTMP/xymon_iostatdisk.$MACHINEDOTS; fi

exit

