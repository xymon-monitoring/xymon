	echo "Checking for RRDtool ..."

	RRDINC=""
	RRDLIB=""
	for DIR in /usr/local/rrdtool /usr/local /usr
	do
		if test -f $DIR/include/rrd.h
		then
			RRDINC=$DIR/include
		fi

		if test -f $DIR/lib/librrd.so
		then
			RRDLIB=$DIR/lib
		fi
		if test -f $DIR/lib/librrd.a
		then
			RRDLIB=$DIR/lib
		fi
	done

	cd build
	OS=`uname -s` make -f Makefile.test-rrd clean
	OS=`uname -s` RRDINC="-I$RRDINC" make -f Makefile.test-rrd test-compile
	if [ $? -eq 0 ]; then
		echo "Found RRDtool include files in $RRDINC"
	else
		echo "ERROR: RRDtool include files found in $RRDINC, but compile fails."
		exit 1
	fi

	OS=`uname -s` RRDLIB="-L$RRDLIB" make -f Makefile.test-rrd test-link
	if [ $? -eq 0 ]; then
		echo "Found RRDtool libraries in $RRDLIB"
	else
		echo "ERROR: RRDtool library files found in $RRDLIB, but link fails."
		exit 1
	fi
	OS=`uname -s` make -f Makefile.test-rrd clean
	cd ..


