#!/bin/sh

# Xymon data collector for MQ
#
#
# Collect data from the MQ utility "runmqsc":
# - queue depth
# - channel status
# for all of the queue managers given as parameters.
#
# Wrap this in a client data message, tagged as 
# coming from an "mqcollect" client collector.
#
# Requires Xymon server ver. 4.3.0
#
#
# Called from hobbitlaunch with
#
#     CMD $BBHOME/ext/mq.sh QUEUEMGR1 [QUEUEMGR2...]
#
# where QUEUEMGR* are the names of the queue managers.
#
# $Id$

TMPFILE="$BBTMP/mq-$MACHINE.$$"

echo "client/mqcollect $MACHINE.mqcollect mqcollect" >$TMPFILE

while test "$1" -ne ""
do
    QMGR=$1; shift
    (echo 'dis ql(*) curdepth'; echo 'dis chs(*)'; echo 'end') | runmqsc $QMGR >> $TMPFILE
done
    
$BB $BBDISP "@" < $TMPFILE
rm $TMPFILE

exit 0

