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
# Graphs LOAD
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
GRAPH="load.$dir"
GRAPHTITLE="$dir CPU Load"

#
################################################
################################################
# Graph Load for these periods :
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
# On s'assure que les rep existent
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
--vertical-label "CPU load" --no-gridfit \
--title "${GRAPHTITLE}" -w 600 -h 120 \
--units-exponent 0 --imgformat PNG \
'DEF:avg='${RRDARCHIVE}'la.rrd:la:AVERAGE' \
'CDEF:la=avg,100,/' \
'AREA:la#00CC00:CPU Load Average' \
'GPRINT:la:LAST: \: %5.1lf (cur)' \
'GPRINT:la:MAX: \: %5.1lf (max)' \
'GPRINT:la:MIN: \: %5.1lf (min)' \
'GPRINT:la:AVERAGE: \: %5.1lf (avg)\n'

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
# End of script Load 
#################################################
