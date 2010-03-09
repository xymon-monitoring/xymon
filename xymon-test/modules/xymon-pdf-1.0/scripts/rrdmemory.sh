#!/bin/sh
#
# rrdtool command line
#
#rrdtool graph filename [--start seconds] [--end seconds] [--vertical-label text] [--width pixels] [--height pixels] [--no-legend] [--title title] [DEF:vname=rrd:ds-name:CF] [CDEF:vname=rpn-expression] [COMMENT:text] [HRULE:value#rrggbb[:legend]] [LINE{1|2|3}:vname[#rrggbb[:legend]]] [AREA:vname[#rrggbb[:legend]]] [STACK:vname[#rrggbb[:legend]]]
#
#
# script under GPL license found in this place.
# http://ed.zehome.com/
#
# rewritten by ThomaS for PDFs reports into Xymon.
# Generate ALL graphes for EVERY servers
#
# Graphs memory 
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
GRAPH="mem.$dir"
GRAPHTITLE="$dir Memory Utilization"

#
################################################
################################################
# Graphs memory for these periods :
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
# We check here presence of actual memory. If this definition is 
# present we are on Linux...
#
if [ -e "$RRDARCHIVE"memory.actual.rrd ]
then

/usr/bin/rrdtool graph "$RRDGRAPHS/$dir/${GRAPH2}" \
--start ${i} \
--vertical-label "% Used" \
--title "${GRAPHTITLE}" -w 600 -h 120 -u "memory" \
--units-exponent 0 --imgformat PNG \
'DEF:avg1='$RRDARCHIVE'memory.actual.rrd:realmempct:AVERAGE' \
'LINE2:avg1#99FFFF:actual' \
'-u 100' \
'-b 1024' \
'GPRINT:avg1:LAST: \: %5.1lf (cur)' \
'GPRINT:avg1:MAX: \: %5.1lf (max)' \
'GPRINT:avg1:MIN: \: %5.1lf (min)' \
'GPRINT:avg1:AVERAGE: \: %5.1lf (avg)\n' \
'DEF:avg2='$RRDARCHIVE'memory.real.rrd:realmempct:AVERAGE' \
'LINE2:avg2#3300CC:real' \
'-u 100' \
'-b 1024' \
'GPRINT:avg2:LAST: \: %5.1lf (cur)' \
'GPRINT:avg2:MAX: \: %5.1lf (max)' \
'GPRINT:avg2:MIN: \: %5.1lf (min)' \
'GPRINT:avg2:AVERAGE: \: %5.1lf (avg)\n' \
'DEF:avg3='$RRDARCHIVE'memory.swap.rrd:realmempct:AVERAGE' \
'LINE2:avg3#9932CC:swap' \
'-u 100' \
'-b 1024' \
'GPRINT:avg3:LAST: \: %5.1lf (cur)' \
'GPRINT:avg3:MAX: \: %5.1lf (max)' \
'GPRINT:avg3:MIN: \: %5.1lf (min)' \
'GPRINT:avg3:AVERAGE: \: %5.1lf (avg)\n'

else

/usr/bin/rrdtool graph "$RRDGRAPHS/$dir/${GRAPH2}" \
--start ${i} \
--vertical-label "% Used" \
--title "${GRAPHTITLE}" -w 600 -h 120 -u "memory" \
--units-exponent 0 --imgformat PNG \
'DEF:avg2='$RRDARCHIVE'memory.real.rrd:realmempct:AVERAGE' \
'LINE2:avg2#FF0000:real' \
'-u 100' \
'-b 1024' \
'GPRINT:avg2:LAST: \: %5.1lf (cur)' \
'GPRINT:avg2:MAX: \: %5.1lf (max)' \
'GPRINT:avg2:MIN: \: %5.1lf (min)' \
'GPRINT:avg2:AVERAGE: \: %5.1lf (avg)\n' \
'DEF:avg3='$RRDARCHIVE'memory.swap.rrd:realmempct:AVERAGE' \
'LINE2:avg3#9932CC:swap' \
'-u 100' \
'-b 1024' \
'GPRINT:avg3:LAST: \: %5.1lf (cur)' \
'GPRINT:avg3:MAX: \: %5.1lf (max)' \
'GPRINT:avg3:MIN: \: %5.1lf (min)' \
'GPRINT:avg3:AVERAGE: \: %5.1lf (avg)\n'

fi

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
## End of script Memory
#################################################
