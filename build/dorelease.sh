#!/bin/sh

SRCDIRS="bbdisplay bbnet bbproxy build common contrib docs hobbitd include lib debian rpm"

case "$1" in
	"tag"|"untag"|"release")
		CMD="$1"
		REL="$2"
		RELDIR=~/hobbit/release/hobbit-$REL
		;;

	"daily")
		CMD="daily"
		REL="snapshot"
		RELDIR=~/hobbit/beta/snapshot
		if [ -d $RELDIR ]; then
			(cd $RELDIR && rm -rf *)
		fi
		;;

	*)
		echo "$0 [tag|untag|release|daily] version"
		exit
esac

cd ~/hobbit
RCSTAG=`echo $REL | sed 's/\./_/g'`
DIRLIST=""
for D in $SRCDIRS; do
    DIRLIST="$DIRLIST `find $D -name RCS | sed -e 's/\/RCS//'|grep -v private|xargs echo`"
done

case "$CMD" in
	"release")
		make distclean
		for f in $DIRLIST
		do
			rcs -nrel_$RCSTAG: ~/hobbit/$f/RCS/*
		done
		;;

	"untag")
		for f in . $DIRLIST
		do
			rcs -nrel_$RCSTAG ~/hobbit/$f/RCS/*
		done
		exit 0
		;;

	"tag")
		for f in $DIRLIST
		do
			rcs -nrel_$RCSTAG: ~/hobbit/$f/RCS/*
		done
		exit 0
		;;

	*)
		;;
esac

# It's a release - copy the files
cd ~/hobbit
mkdir $RELDIR
for f in $SRCDIRS
do
	find $f/ | grep -v RCS | cpio -pdvmu $RELDIR/
done
cp -p Changes configure COPYING CREDITS README $RELDIR/
find $RELDIR -type d|xargs chmod 755
cd $RELDIR && make -f ~/hobbit/Makefile.osiris distclean
cd $RELDIR && rm -f {debian,rpm}/pkg/*
cd $RELDIR/../ && tar zcf hobbit-$REL.tar.gz `basename $RELDIR`

exit 0

