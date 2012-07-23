	echo "Checking for RRDtool ..."

	RRDDEF=""
	RRDINC=""
	RRDLIB=""
	PNGLIB=""
	ZLIB=""
	for DIR in /opt/rrdtool* /usr/local/rrdtool* /usr/local /usr/pkg /opt/csw /opt/sfw /usr/sfw
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

	# See if it builds
	RRDOK="YES"
	if test "$RRDINC" != ""; then INCOPT="-I$RRDINC"; fi
	if test "$RRDLIB" != ""; then LIBOPT="-L$RRDLIB"; fi
	cd build
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-rrd clean
	OS=`uname -s | tr '[/]' '[_]'` RRDDEF="$RRDDEF" RRDINC="$INCOPT" $MAKE -f Makefile.test-rrd test-compile 2>/dev/null
	if test $? -ne 0; then
		# See if it's the new RRDtool 1.2.x
		echo "Not RRDtool 1.0.x, checking for 1.2.x"
		RRDDEF="-DRRDTOOL12"
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-rrd clean
		OS=`uname -s | tr '[/]' '[_]'` RRDDEF="$RRDDEF" RRDINC="$INCOPT" $MAKE -f Makefile.test-rrd test-compile
	fi
	if test $? -eq 0; then
		echo "Compiling with RRDtool works OK"
	else
		echo "ERROR: Cannot compile with RRDtool."
	fi

	OS=`uname -s | tr '[/]' '[_]'` RRDLIB="$LIBOPT" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
	if test $? -ne 0; then
		# Could be that we need -lz for RRD
		PNGLIB="$PNGLIB $ZLIB"
	fi
	OS=`uname -s | tr '[/]' '[_]'` RRDLIB="$LIBOPT" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
	if test $? -ne 0; then
		# Could be that we need -lm for RRD
		PNGLIB="$PNGLIB -lm"
	fi
	OS=`uname -s | tr '[/]' '[_]'` RRDLIB="$LIBOPT" PNGLIB="$PNGLIB" $MAKE -f Makefile.test-rrd test-link 2>/dev/null
	if test $? -eq 0; then
		echo "Linking with RRDtool works OK"
		if test "$PNGLIB" != ""; then
			echo "Linking RRD needs extra library: $PNGLIB"
		fi
	else
		echo "ERROR: Linking with RRDtool fails"
	fi
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-rrd clean
	cd ..

	if test "$RRDOK" = "NO"; then
		echo "RRDtool include- or library-files not found. These are REQUIRED for Xymon"
		echo "RRDtool can be found at http://www.mrtg.org/rrdtool/"
		echo "If you have RRDtool installed, use the \"--rrdinclude DIR\" and \"--rrdlib DIR\""
		echo "options to configure to specify where they are."
		exit 1
	fi


