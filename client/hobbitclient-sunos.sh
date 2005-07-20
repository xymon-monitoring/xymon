#!/bin/sh

# Experimental Solaris client for Hobbit

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[df]"
/usr/xpg4/bin/df -F ufs -k
echo "[prtconf]"
/usr/sbin/prtconf
echo "[memory]"
vmstat 1 2 | tail -1
echo "[swap]"
/usr/sbin/swap -s
echo "[netstat]"
netstat -s
echo "[ps]"
ps -ef
echo "[top]"
top -b 20
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi

exit

