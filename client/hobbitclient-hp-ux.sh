#!/bin/sh

# Experimental HP-UX client for Hobbit

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
df -Pk
echo "[memory]"
echo "Total:`bb-hp-memsz -p`"
echo "Free:`bb-hp-memsz -f`"
echo "[swapinfo]"
/usr/sbin/swapinfo -tm
echo "[netstat]"
netstat -s
echo "[ps]"
ps -ef
echo "[top]"
top -d 20
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi

exit

