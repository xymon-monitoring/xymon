#!/bin/sh

echo "client $MACHINE.$BBOSTYPE"      >  $BBTMP/msg.txt
$BBHOME/bin/hobbitclient-$BBOSTYPE.sh >> $BBTMP/msg.txt
$BB $BBDISP "@" < $BBTMP/msg.txt

exit 0

