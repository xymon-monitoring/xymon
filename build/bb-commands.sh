#!/bin/sh

# $Id: bb-commands.sh,v 1.2 2005-02-16 21:17:12 henrik Exp $

# Script to pick up most of the commands used by BB extension scripts.
# This is used during installation, to build a hobbitserver.cfg that
# includes these commands so that extension scripts can run from
# hobbitserver.cfg without having to do special setups.

findbin() {
	MYP="`echo ${PATH} | sed -e 's/:/ /g'`"

	for D in $MYP
	do
		if test -x $D/$1; then
			echo "${D}/${1}"
		fi
	done
}

echo ""
echo "# The following defines a bunch of commands that BB extensions expect to be present."
echo "# Hobbit does not use them, but they are provided here so if you use BB extension"
echo "# scripts, then they will hopefully run without having to do a lot of tweaking."
echo ""
for CMD in top uptime awk cat cp cut date egrep expr find grep head id ln ls mv rm sed sort tail touch tr uniq who
do
	ENVNAME=`echo $CMD | tr "[a-z]" "[A-Z]"`
	PGM=`findbin $CMD | head -1`
	echo "${ENVNAME}=${PGM}"
done

# WC is special
PGM=`findbin wc | head -1`
echo "WC=${PGM} -l"
echo "WCC=${PGM}"

# DFCMD is an alias for DF
echo "# You should check that this returns diskstats in KB (Posix output)"
PGM=`findbin df | head -1`
echo "DF=${PGM} -Pk"
echo "DFCMD=${PGM} -Pk"

# PS probably needs tweaking
echo "# Adjust ps output to your liking"
echo "PS=ps ax"

echo ""
echo "# Miscellaneous definitions used by the BB client"
echo "MAXLINE=32768"

echo ""
echo "# For the cpu check"
echo "DOCPU=TRUE"
echo "NOCPUCOLOR=clear"
echo "BBCPUTAB=\$BBHOME/etc/bb-cputab"
echo "BBTOOMANYDAYSUP=9999"
echo "CPUPANIC=300"
echo "CPUWARN=150"
echo "DISPREALLOADAVG=FALSE"
echo "TOPARGS=-b -n 1"
echo "WARNCOLORONREBOOT=yellow"
echo "WARNMINSONREBOOT=60"

echo ""
echo "# For the disk check"
echo "DODISK=TRUE"
echo "NODISKCOLOR=clear"
echo "BBDFTAB=\$BBHOME/etc/bb-dftab"
echo "DFEXCLUDE=cdrom"
echo "DFPANIC=95"
echo "DFSORT=4"
echo "DFUSE=^/dev"
echo "DFWARN=90"

echo ""
echo "# For the msgs check"
echo "DOMSGS=TRUE"
echo "NOMSGSCOLOR=clear"
echo "BBMSGSTAB=\$BBHOME/etc/bb-msgstab"
echo "CHKMSGLEN=TRUE"
echo "IGNMSGS="
echo "MSGEXPIRE=30:60"
echo "MSGFILE=/var/log/messages"
echo "MSGS=NOTICE WARNING"
echo "PAGEMSG=NOTICE"
echo "REDMSGSLINES=20"
echo "YELLOWMSGSLINES=10"

echo ""
echo "# For the procs check"
echo "DOPROCS=TRUE"
echo "NOPROCSCOLOR=clear"
echo "BBPROCTAB=\$BBHOME/etc/bb-proctab"
echo "PAGEPROC=cron"
echo "PROCS=bbrun"

