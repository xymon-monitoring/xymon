#!/bin/sh

# $Id: bb-commands.sh,v 1.6 2006-06-21 05:50:38 henrik Exp $

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
for CMD in uptime awk cat cp cut date egrep expr find grep head id ln ls mv rm sed sort tail top touch tr uniq who
do
	ENVNAME=`echo $CMD | tr "[a-z]" "[A-Z]"`
	PGM=`findbin $CMD | head -n 1`
	echo "${ENVNAME}=\"${PGM}\""
done

# WC is special
PGM=`findbin wc | head -n 1`
echo "WC=\"${PGM} -l\""
echo "WCC=\"${PGM}\""

# DFCMD is an alias for DF
PGM=`findbin df | head -n 1`
echo "# DF,DFCMD and PS are for compatibility only, NOT USED by the Hobbit client"
echo "DF=\"${PGM} -Pk\""
echo "DFCMD=\"${PGM} -Pk\""
echo "PS=\"ps ax\""
echo ""
echo "MAXLINE=\"32768\""

