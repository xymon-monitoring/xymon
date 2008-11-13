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
# Graphs Run-Queue
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
GRAPH="rq.$dir"
GRAPHTITLE="$dir Run-queue Length"

#
################################################
################################################
# Graphs Run-Queue for these periods :
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
/usr/bin/rrdtool graph "$RRDGRAPHS/$dir/${GRAPH2}" \
--start ${i} \
--vertical-label "#" \
--title "${GRAPHTITLE}" -w 600 -h 120 -u "rq" \
--units-exponent 0 --imgformat PNG \
'DEF:rq='${RRDARCHIVE}'sar.rrd:sar_rq:AVERAGE' \
'LINE2:rq#33CC99: Length \n' \
'GPRINT:rq:LAST:cur \:%3.2lf' \
'GPRINT:rq:MIN:min \:%3.2lf' \
'GPRINT:rq:AVERAGE:avg \:%3.2lf' \
'GPRINT:rq:MAX:max \:%3.2lf' 

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
# End of script Run-Queue
#################################################
