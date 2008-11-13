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
# Graphs Vmstat
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
GRAPH="cputil.$dir"
GRAPHTITLE="$dir CPU Utilization"

#
################################################
################################################
# Graphs Vmstat for these periods :
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
--vertical-label "% CPU Load" \
--title "${GRAPHTITLE}" -w 600 -h 120 -u "cputil" \
--units-exponent 0 --imgformat PNG \
'-u 100' \
'-r' \
'DEF:cpu_idl='${RRDARCHIVE}'vmstat.rrd:cpu_idl:AVERAGE' \
'DEF:cpu_usr='${RRDARCHIVE}'vmstat.rrd:cpu_usr:AVERAGE' \
'DEF:cpu_sys='${RRDARCHIVE}'vmstat.rrd:cpu_sys:AVERAGE' \
'AREA:cpu_sys#FF0000:system' \
'STACK:cpu_usr#FFFF00:user' \
'STACK:cpu_idl#00FF00:idle' \
'COMMENT:\n' \
'GPRINT:cpu_sys:LAST:system  \: %5.1lf (cur)' \
'GPRINT:cpu_sys:MAX: \: %5.1lf (max)' \
'GPRINT:cpu_sys:MIN: \: %5.1lf (min)' \
'GPRINT:cpu_sys:AVERAGE: \: %5.1lf (avg)\n' \
'GPRINT:cpu_usr:LAST:user    \: %5.1lf (cur)' \
'GPRINT:cpu_usr:MAX: \: %5.1lf (max)' \
'GPRINT:cpu_usr:MIN: \: %5.1lf (min)' \
'GPRINT:cpu_usr:AVERAGE: \: %5.1lf (avg)\n' \
'GPRINT:cpu_idl:LAST:idle    \: %5.1lf (cur)' \
'GPRINT:cpu_idl:MAX: \: %5.1lf (max)' \
'GPRINT:cpu_idl:MIN: \: %5.1lf (min)' \
'GPRINT:cpu_idl:AVERAGE: \: %5.1lf (avg)\n'

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
# End of script Vmstat
#################################################
