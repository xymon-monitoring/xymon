	echo "Checking for zlib ..."

	ZLIBINC=""
	ZLIBLIB=""
	for DIR in /opt/zlib* /usr/local/zlib* /usr/local /usr/pkg /opt/csw /opt/sfw
	do
		if test -f $DIR/include/zlib.h
		then
			ZLIBINC=$DIR/include
		fi
		if test -f $DIR/include/zlib/zlib.h
		then
			ZLIBINC=$DIR/include/zlib
		fi

		if test -f $DIR/lib/libz.so
		then
			ZLIBLIB=$DIR/lib
		fi
		if test -f $DIR/lib/libz.a
		then
			ZLIBLIB=$DIR/lib
		fi
		if test -f $DIR/lib64/libz.so
		then
			ZLIBLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/libz.a
		then
			ZLIBLIB=$DIR/lib64
		fi
	done

	if test "$USERZLIBINC" != ""; then
		ZLIBINC="$USERZLIBINC"
	fi
	if test "$USERZLIBLIB" != ""; then
		ZLIBLIB="$USERZLIBLIB"
	fi

	# Lets see if it can build
	ZLIBOK="YES"
	cd build
	if test ! -z $ZLIBINC; then INCOPT="-I$ZLIBINC"; fi
	if test ! -z $ZLIBLIB; then LIBOPT="-L$ZLIBLIB"; fi
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-zlib clean
	OS=`uname -s | tr '[/]' '[_]'` ZLIBINC="$INCOPT" $MAKE -f Makefile.test-zlib test-compile
	if test $? -eq 0; then
		echo "Compiling with ZLIB library works OK"
	else
		echo "ERROR: Cannot compile using ZLIB library."
		ZLIBOK="NO"
	fi

	OS=`uname -s | tr '[/]' '[_]'` ZLIBLIB="$LIBOPT" $MAKE -f Makefile.test-zlib test-link
	if test $? -eq 0; then
		echo "Linking with ZLIB library works OK"
	else
		echo "ERROR: Cannot link with ZLIB library."
		ZLIBOK="NO"
	fi
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-zlib clean
	cd ..

	if test "$ZLIBOK" = "NO"; then
		echo "Missing ZLIB include- or library-files. These are REQUIRED for xymond"
		echo "ZLIB can be found at http://www.zlib.org/"
		echo "If you have ZLIB installed, use the \"--zlibinclude DIR\" and \"--zliblib DIR\""
		echo "options to configure to specify where they are."
		exit 1
	fi

