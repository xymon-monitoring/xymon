	echo "Checking for RRDtool ..."

	RRDDEF=""
	RRDINC=""
	RRDLIB=""
	PNGLIB=""
	ZLIB=""
	for DIR in /opt/rrdtool* /usr/local/rrdtool* /usr/local /usr /usr/pkg /opt/csw /opt/sfw /usr/sfw
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
		if test -f $DIR/lib64/librrd.so
		then
			RRDLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/librrd.a
		then
			RRDLIB=$DIR/lib64
		fi

		if test -f $DIR/lib/libpng.so
		then
			PNGLIB="-L$DIR/lib -lpng"
		fi
		if test -f $DIR/lib/libpng.a
		then
			PNGLIB="-L$DIR/lib -lpng"
		fi
		if test -f $DIR/lib64/libpng.so
		then
			PNGLIB="-L$DIR/lib64 -lpng"
		fi
		if test -f $DIR/lib64/libpng.a
		then
			PNGLIB="-L$DIR/lib64 -lpng"
		fi

		if test -f $DIR/lib/libz.so
		then
			ZLIB="-L$DIR/lib -lz"
		fi
		if test -f $DIR/lib/libz.a
		then
			ZLIB="-L$DIR/lib -lz"
		fi
		if test -f $DIR/lib64/libz.so
		then
			ZLIB="-L$DIR/lib64 -lz"
		fi
		if test -f $DIR/lib64/libz.a
		then
			ZLIB="-L$DIR/lib64 -lz"
		fi
	done

	if test "$USERRRDINC" != ""; then
		RRDINC="$USERRRDINC"
	fi
	if test "$USERRRDLIB" != ""; then
		RRDLIB="$USERRRDLIB"
	fi

	if test -z "$RRDINC" -o -z "$RRDLIB"; then
		echo "RRDtool include- or library-files not found. These are REQUIRED for Xymon"
		echo "RRDtool can be found at http://www.mrtg.org/rrdtool/"
		echo "If you have RRDtool installed, use the \"--rrdinclude DIR\" and \"--rrdlib DIR\""
		echo "options to configure to specify where they are."
		exit 1
	else
		cd build
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-rrd clean
		OS=`uname -s | tr '[/]' '[_]'` RRDDEF="$RRDDEF" RRDINC="-I$RRDINC" $MAKE -f Makefile.test-rrd test-compile
		if [ $? -ne 0 ]; then
			# See if it's the new RRDtool 1.2.x
			echo "Not RRDtool 1.0.x, checking for 1.2.x"
			RRDDEF="-DRRDTOOL12"
			OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-rrd clean
			OS=`uname -s | tr '[/]' '[_]'` RRDDEF="$RRDDEF" RRDINC="-I$RRDINC" $MAKE -f Makefile.test-rrd test-compile
		fi
		if [ $? -eq 0 ]; then
			echo "Found RRDtool include files in $RRDINC"
		else
			echo "ERROR: RRDtool include files found in $RRDINC, but compile fails."
			exit 1
		fi

		OS=`uname -s | tr '[/]' '[_]'` RRDLIB="-L$RRDLIB" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
		if [ $? -ne 0 ]; then
			# Could be that we need -lz for RRD
			PNGLIB="$PNGLIB $ZLIB"
		fi
		OS=`uname -s | tr '[/]' '[_]'` RRDLIB="-L$RRDLIB" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
		if [ $? -ne 0 ]; then
			# Could be that we need -lm for RRD
			PNGLIB="$PNGLIB -lm"
		fi
		OS=`uname -s | tr '[/]' '[_]'` RRDLIB="-L$RRDLIB" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
		if [ $? -eq 0 ]; then
			echo "Found RRDtool libraries in $RRDLIB"
			if test "$PNGLIB" != ""; then
				echo "Linking RRD with PNG library: $PNGLIB"
			fi
		else
			echo "ERROR: RRDtool library files found in $RRDLIB, but link fails."
			exit 1
		fi
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-rrd clean
		cd ..
	fi


