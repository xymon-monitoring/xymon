#!/bin/sh

case "$1" in
	"tag"|"untag"|"release")
		CMD="$1"
		REL="$2"
		;;

	*)
		CMD="release"
		REL="$1"
		;;
esac

if [ "$REL" = "" -o "$CMD" = "" ]; then
	echo "$0 [tag|untag] version"
	exit
fi

cd ~/hobbit
if [ "$CMD" = "release" ]; then make distclean; fi

RCSTAG=`echo $REL | sed 's/\./_/g'`
DIRLIST=`find . -name RCS | sed -e 's/\/RCS//'|grep -v private|xargs echo`

if [ "$CMD" = "untag" ]
then
	for f in . $DIRLIST
	do
		rcs -nrel_$RCSTAG ~/hobbit/$f/RCS/*
	done

	exit 0
fi

# It's tag or release - so tag the files.
for f in $DIRLIST
do
	rcs -nrel_$RCSTAG: ~/hobbit/$f/RCS/*
done

if [ "$CMD" = "tag" ]
then
	exit 0
fi

# It's a release - copy the files
cd ~/hobbit
mkdir ~/hobbit/release/hobbit-$REL
for f in bbdisplay bbnet bbpatches bbproxy build common docs hobbitd include lib scripts
do
	find $f/ | grep -v RCS | cpio -pdvmu ~/hobbit/release/hobbit-$REL/
done
cp -p Changes.bbgen Changes configure COPYING CREDITS README ~/hobbit/release/hobbit-$REL/
find ~/hobbit/release/hobbit-$REL -type d|xargs chmod 755

exit 0

