#!/bin/sh

# Experimental Linux client for Hobbit

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
df -Pl -x none -x tmpfs -x shmfs
echo "[free]"
free
echo "[netstat]"
netstat -s
echo "[ps]"
ps -ef
echo "[top]"
top -b -n 1 
# vmstat
nohup sh -c "vmstat 300 2 1>$BBTMP/hobbit_vmstat.$$ 2>&1; mv $BBTMP/hobbit_vmstat.$$ $BBTMP/hobbit_vmstat" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $BBTMP/hobbit_vmstat; then echo "[vmstat]"; cat $BBTMP/hobbit_vmstat; rm -f $BBTMP/hobbit_vmstat; fi

exit

