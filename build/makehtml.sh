#!/bin/bash

export LANG=C
DATE=`date +"%e %b %Y"`
VERSION="$1"
if [ "$VERSION" = "" ]
then
	VERSION="Exp"
fi

cd ~/hobbit

for DIR in bbdisplay bbnet bbproxy common hobbitd
do
	for SECT in 1 5 7 8
	do
		for FILE in $DIR/*.$SECT
		do
			if [ -r $FILE ]
			then
				NAME=`head -1 $FILE | awk '{print $2}'`;
				SECTION=`head -1 $FILE | awk '{print $3}'`;
				(echo ".TH $NAME $SECTION \"Version $VERSION: $DATE\" \"Hobbit Monitor\""; tail +2 $FILE) | \
				man2html -r - | tail +2 >docs/manpages/man$SECT/`basename $FILE`.html
			fi
		done
	done

	# for FILE in $DIR/html_*.8; do if [ -r $FILE ]; then man2html -r $FILE | tail --lines=+2 >docs/manpages/man8/`basename $FILE`.html; fi; done
done

