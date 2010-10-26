#!/bin/sh

SRCDIRS="xymongen xymonnet xymonproxy build client common contrib docs xymond web include lib debian rpm demotool"

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
		for f in . $DIRLIST
		do
			# Tag all current versions a "Release"
			rcs -sRel ~/hobbit/$f/*
			# Tag the current version with the release number
			rcs -nrel_$RCSTAG: ~/hobbit/$f/RCS/*
			# Checkout the current version
			pushd ~/hobbit/$f && co RCS/* && popd
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
		for f in . $DIRLIST
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
cp -p Changes configure configure.server configure.client COPYING CREDITS README README.CLIENT RELEASENOTES $RELDIR/
find $RELDIR -type d|xargs chmod 755
cd $RELDIR && make -f ~/hobbit/Makefile.home distclean
cd $RELDIR && rm -f {debian,rpm}/pkg/*
cd $RELDIR/../ && tar zcf hobbit-$REL.tar.gz `basename $RELDIR`

# Change version number for snapshots
if [ "$CMD" = "daily" ]; then
	TSTAMP=`date +"%Y%m%d%H%M%S"`
	if [ -f /tmp/version.h ]; then rm -f /tmp/version.h; fi
	cp $RELDIR/include/version.h /tmp/
	rm -f $RELDIR/include/version.h
	cat /tmp/version.h | sed -e "s/define VERSION.*/define VERSION \"0.$TSTAMP\"/" >$RELDIR/include/version.h
	DAYAGO=`date +"%Y%m%d" --date=yesterday`
	WEEKAGO=`date +"%Y%m%d" --date="7 days ago"`
	~/hobbit/build/listchanges.sh $DAYAGO >$RELDIR/changelog-yesterday.txt
	~/hobbit/build/listchanges.sh $WEEKAGO >$RELDIR/changelog-lastweek.txt
fi

exit 0

