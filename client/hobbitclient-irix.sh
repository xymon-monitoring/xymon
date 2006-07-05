#!/bin/sh
#----------------------------------------------------------------------------#
# Irix client for Hobbit                                                     #
#                                                                            #
# Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: hobbitclient-irix.sh,v 1.5 2006-07-05 05:52:22 henrik Exp $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
df -Plk
echo "[mount]"
mount
echo "[swap]"
swap -ln
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
netstat -i -n | egrep -v  "^lo|<Link"
echo "[ps]"
ps -Axo pid,ppid,user,stime,state,nice,pcpu,time,sz,rss,vsz,args

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
	$TOP -d2 -b 20 | tail +9
    fi
fi

# vmstat and iostat do not exist on irix. SAR is your only option at this time. 
nohup sh -c "sar 300 2 1>$BBTMP/hobbit_sar.$MACHINEDOTS.$$ 2>&1; mv $BBTMP/hobbit_sar.$MACHINEDOTS.$$ $BBTMP/hobbit_sar.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_sar.$MACHINEDOTS; then echo "[sar]"; cat $BBTMP/hobbit_sar.$MACHINEDOTS; rm -f $BBTMP/hobbit_sar.$MACHINEDOTS; fi

exit

