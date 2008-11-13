#!/bin/sh
#
# rrdtool command line
#
# script under GPL license found in this place.
# http://ed.zehome.com/
#
# rewritten by ThomaS for PDFs reports into Xymon.
# Generate ALL graphes for EVERY servers
#
# Graphs Network I/O
#
################################################
# Configuration
#
source config

#
# 'dir' is the hostname of server
#
for dir in `ls $RRDREP` ;
do

RRDARCHIVE="$RRDREP/$dir/"

# .png IS ADDED AFTER.
# ${GRAPH}-day -week -2week -month -year will be created
GRAPH="netio.$dir"
GRAPHTITLE="$dir Network Traffic"

#
################################################
################################################
# Graphs network I/O for these periods :
#
# 1 day, 1 week, 2 weeks, 1 month, 1 year
#

for i in -86400 -604800 -1296000 -2592000 -31536000
do

if [ $i == -86400 ]
then
GRAPH2="${GRAPH}-day.png"
fi

if [ $i == -604800 ]
then
GRAPH2="${GRAPH}-week.png"
fi

if [ $i == -1296000 ]
then
GRAPH2="${GRAPH}-2week.png"
fi

if [ $i == -2592000 ]
then
GRAPH2="${GRAPH}-month.png"
fi

if [ $i == -31536000 ]
then
GRAPH2="${GRAPH}-year.png"
fi

#
# Check for directories.
#
if [ ! -d $RRDGRAPHS/$dir ]
then
   mkdir $RRDGRAPHS/$dir
fi

#
# Look at $XYMONSERVERHOME/etc/hobbitgraphs.cfg
# to find the right parameters and how to use them.
#
# RRD definition for Linux
/usr/bin/rrdtool graph "$RRDGRAPHS/$dir/$GRAPH2" \
--start $i \
--vertical-label "MBits/second" \
--title "$GRAPHTITLE" -w 600 -h 120 -M \
-u 1.0 --imgformat PNG \
'DEF:inbyteseth0='${RRDARCHIVE}'ifstat.eth0.rrd:bytesReceived:AVERAGE' \
'CDEF:ineth0=inbyteseth0,8,*,1000000,/' \
'DEF:outbyteseth0='${RRDARCHIVE}'ifstat.eth0.rrd:bytesSent:AVERAGE' \
'CDEF:outeth0=outbyteseth0,8,*,1000000,/' \
'DEF:inbyteseth1='${RRDARCHIVE}'ifstat.eth1.rrd:bytesReceived:AVERAGE' \
'CDEF:ineth1=inbyteseth1,8,*,1000000,/' \
'DEF:outbyteseth1='${RRDARCHIVE}'ifstat.eth1.rrd:bytesSent:AVERAGE' \
'CDEF:outeth1=outbyteseth1,8,*,1000000,/' \
'LINE2:ineth0#00FF00:eth0 inbound' \
'GPRINT:ineth0:LAST:  \: %3.3lf (cur)' \
'GPRINT:ineth0:MAX: \: %3.3lf (max)' \
'GPRINT:ineth0:MIN: \: %3.3lf (min)' \
'GPRINT:ineth0:AVERAGE: \: %3.3lf (avg)\n' \
'LINE2:outeth0#0000FF:eth0 outbound' \
'GPRINT:outeth0:LAST: \: %3.3lf (cur)' \
'GPRINT:outeth0:MAX: \: %3.3lf (max)' \
'GPRINT:outeth0:MIN: \: %3.3lf (min)' \
'GPRINT:outeth0:AVERAGE: \: %3.3lf (avg)\n' \
'LINE2:ineth1#FF0000:eth1 inbound' \
'GPRINT:ineth1:LAST:  \: %3.3lf (cur)' \
'GPRINT:ineth1:MAX: \: %3.3lf (max)' \
'GPRINT:ineth1:MIN: \: %3.3lf (min)' \
'GPRINT:ineth1:AVERAGE: \: %3.3lf (avg)\n' \
'LINE2:outeth1#F781F3:eth1 outbound' \
'GPRINT:outeth1:LAST: \: %3.3lf (cur)' \
'GPRINT:outeth1:MAX: \: %3.3lf (max)' \
'GPRINT:outeth1:MIN: \: %3.3lf (min)' \
'GPRINT:outeth1:AVERAGE: \: %3.3lf (avg)\n'

# RRD definition for proprietary UNIX
#
#'DEF:tcpInInorderBits='$RRDARCHIVE'netstat.rrd:tcpInInorderBytes:AVERAGE' \
#'DEF:tcpOutDataBits='$RRDARCHIVE'netstat.rrd:tcpOutDataBytes:AVERAGE' \
#'DEF:tcpRetransBits='$RRDARCHIVE'netstat.rrd:tcpRetransBytes:AVERAGE' \
#'LINE2:tcpInInorderBits#00FF00:In' \
#'LINE2:tcpOutDataBits#0000FF:Out' \
#'LINE2:tcpRetransBits#FF0000:Retrans' \
#'COMMENT:\n' \
#'GPRINT:tcpInInorderBits:LAST:In       \: %5.1lf%s (cur)' \
#'GPRINT:tcpInInorderBits:MAX: \: %5.1lf%s (max)' \
#'GPRINT:tcpInInorderBits:MIN: \: %5.1lf%s (min)' \
#'GPRINT:tcpInInorderBits:AVERAGE: \: %5.1lf%s (avg)\n' \
#'GPRINT:tcpOutDataBits:LAST:Out      \: %5.1lf%s (cur)' \
#'GPRINT:tcpOutDataBits:MAX: \: %5.1lf%s (max)' \
#'GPRINT:tcpOutDataBits:MIN: \: %5.1lf%s (min)' \
#'GPRINT:tcpOutDataBits:AVERAGE: \: %5.1lf%s (avg)\n' \
#'GPRINT:tcpRetransBits:LAST:Retrans  \: %5.1lf%s (cur)' \
#'GPRINT:tcpRetransBits:MAX: \: %5.1lf%s (max)' \
#'GPRINT:tcpRetransBits:MIN: \: %5.1lf%s (min)' \
#'GPRINT:tcpRetransBits:AVERAGE: \: %5.1lf%s (avg)\n'

done

###################################################################################
# RRDTOOL 1.2.x produces PNG graphs with alpha transparency. It's good and
# beautiful but FPDF doesn't support it at the time writing. So, removing this
# alpha channel is mandatory and take a lot of resources.
#
# 'mogrify' or 'convert' can be used to remove the alpha channel of graphs.
# Be careful ! The load of your machine will explode...
###################################################################################
#
#if [ $RRDNEW == "Y" ]
#then
#	mogrify -type Palette $RRDGRAPHS/$dir/*.png
#fi

done

#
# End of script Network I/O
#################################################
