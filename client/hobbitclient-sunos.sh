#!/bin/sh
#----------------------------------------------------------------------------#
# Solaris client for Hobbit                                                  #
#                                                                            #
# Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-sunos.sh,v 1.10 2006-04-19 20:17:06 henrik Exp $

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
FSTYPES=`/bin/df -n|awk '{print $3}'|egrep -v "^proc|^fd|^mntfs"|sort|uniq`
if test "$FSTYPES" = ""; then FSTYPES="ufs"; fi
set $FSTYPES
/bin/df -F $1 -k
shift
while test "$1" != ""; do
	/bin/df -F $1 -k|tail +2
	shift
done

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
/usr/bin/kstat -p -s '[or]bytes64'
echo "[ps]"
ps -ef
echo "[top]"
top -b 20
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi
# logfiles
$BBHOME/bin/logfetch $BBTMP/logfetch.cfg $BBTMP/logfetch.status

exit

