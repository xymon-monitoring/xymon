#!/bin/sh
#
#----------------------------------------------------------------------------#
# Darwin (Mac OS X) client for Xymon                                         #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id$

# Use LANG=C, since some locales have different numeric delimiters
# causing the Xymon load-average calculation to fail
LANG=C
export LANG

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who

FILESYSTEMS=`mount | grep -v nobrowse | awk '{print $3}'`
echo "[df]"
set $FILESYSTEMS
(df -P -H $1; shift
 while test $# -gt 0
 do
   df -P -H $1 | tail -1
   shift
 done) | column -t -s " " | sed -e 's!Mounted *on!Mounted on!'

echo "[inode]"
set $FILESYSTEMS
(df -i $1; shift
 while test $# -gt 0
 do
   df -P -H $1 | tail -1
   shift
 done) | awk '
NR<2{printf "%-20s %10s %10s %10s %10s %s\n", $1, "itotal", $6, $7, $8, $9} 
(NR>=2 && $6>0) {printf "%-20s %10d %10d %10d %10s %s\n", $1, $6+$7, $6, $7, $8, $9}'

echo "[mount]"
mount
echo "[meminfo]"
vm_stat
echo "[ifconfig]"
ifconfig -a
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ifstat]"
netstat -ibn | egrep -v "^lo|<Link"
echo "[ports]"
netstat -an|grep "^tcp"
echo "[ps]"
ps -ax -ww -o pid,ppid,user,start,state,pri,pcpu,time,pmem,rss,vsz,command

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != "" -a "$AWK" != ""
then
    if test -x "$TOP" -a -x "$AWK"
    then
        echo "[top]"
	$TOP -l 2 -n 20 -o cpu | $AWK '/^Processes:/ {toprun++} toprun == 2'
    fi
fi

exit

