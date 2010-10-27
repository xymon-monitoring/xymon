#!/bin/sh

SRCDIRS="xymongen xymonnet xymonproxy build client common contrib docs xymond web include lib debian rpm demotool"

case "$1" in
	"tag"|"untag"|"release")
		CMD="$1"
		REL="$2"
		RELDIR=~/xymon/release/xymon-$REL
		;;

	"daily")
		CMD="daily"
		REL="snapshot"
		RELDIR=~/xymon/beta/snapshot
		if [ -d $RELDIR ]; then
			(cd $RELDIR && rm -rf *)
		fi
		;;

	*)
		echo "$0 [tag|untag|release|daily] version"
		exit
esac

cd ~/xymon
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
			rcs -sRel ~/xymon/$f/*
			# Tag the current version with the release number
			rcs -nrel_$RCSTAG: ~/xymon/$f/RCS/*
			# Checkout the current version
			pushd ~/xymon/$f && co RCS/* && popd
		done
		;;

	"untag")
		for f in . $DIRLIST
		do
			rcs -nrel_$RCSTAG ~/xymon/$f/RCS/*
		done
		exit 0
		;;

	"tag")
		for f in . $DIRLIST
		do
			rcs -nrel_$RCSTAG: ~/xymon/$f/RCS/*
		done
		exit 0
		;;

	*)
		;;
esac

# It's a release - copy the files
cd ~/xymon
mkdir $RELDIR
for f in $SRCDIRS
do
	find $f/ | grep -v RCS | cpio -pdvmu $RELDIR/
done
cp -p Changes configure configure.server configure.client COPYING CREDITS README README.CLIENT RELEASENOTES $RELDIR/
find $RELDIR -type d|xargs chmod 755
cd $RELDIR && make -f ~/xymon/Makefile.home distclean
cd $RELDIR && rm -f {debian,rpm}/pkg/*
cd $RELDIR/../ && tar zcf xymon-$REL.tar.gz `basename $RELDIR`

# Change version number for snapshots
if [ "$CMD" = "daily" ]; then
	TSTAMP=`date +"%Y%m%d%H%M%S"`
	if [ -f /tmp/version.h ]; then rm -f /tmp/version.h; fi
	cp $RELDIR/include/version.h /tmp/
	rm -f $RELDIR/include/version.h
	cat /tmp/version.h | sed -e "s/define VERSION.*/define VERSION \"0.$TSTAMP\"/" >$RELDIR/include/version.h
	DAYAGO=`date +"%Y%m%d" --date=yesterday`
	WEEKAGO=`date +"%Y%m%d" --date="7 days ago"`
	~/xymon/build/listchanges.sh $DAYAGO >$RELDIR/changelog-yesterday.txt
	~/xymon/build/listchanges.sh $WEEKAGO >$RELDIR/changelog-lastweek.txt
fi

exit 0

