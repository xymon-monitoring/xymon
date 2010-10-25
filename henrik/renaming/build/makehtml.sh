#!/bin/bash

export LANG=C
DATE=`date +"%e %b %Y"`
VERSION="$1"
if [ "$VERSION" = "" ]
then
	VERSION="Exp"
fi

# cd ~/hobbit
rm -f docs/*~ docs/manpages/index.html* docs/manpages/man1/* docs/manpages/man5/* docs/manpages/man7/* docs/manpages/man8/*

for DIR in bbdisplay xymonnet xymonproxy common xymond web
do
	for SECT in 1 5 7 8
	do
		for FILE in $DIR/*.$SECT
		do
			if [ -r $FILE ]
			then
				NAME=`head -n 1 $FILE | awk '{print $2}'`;
				SECTION=`head -n 1 $FILE | awk '{print $3}'`;
				(echo ".TH $NAME $SECTION \"Version $VERSION: $DATE\" \"Xymon\""; tail -n +2 $FILE) | \
				man2html -r - | tail -n +2 >docs/manpages/man$SECT/`basename $FILE`.html
			fi
		done
	done
done

# Sourceforge update
# cd ~/hobbit/docs && rsync -av --rsh=ssh --exclude=RCS ./ storner@shell.sourceforge.net:/home/groups/h/ho/hobbitmon/htdocs/docs/

