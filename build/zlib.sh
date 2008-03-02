	echo "Checking for zlib ..."

	ZLIBINC=""
	ZLIBLIB=""
	for DIR in /opt/zlib* /usr/local/zlib* /usr/local /usr /usr/pkg /opt/csw /opt/sfw
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

	if test -z "$ZLIBINC" -o -z "$ZLIBLIB"; then
		echo "ZLIB include- or library-files not found."
		echo "Support for compressed communication with hobbitd is disabled "
	else
		cd build
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-zlib clean
		OS=`uname -s | tr '[/]' '[_]'` ZLIBINC="-I$ZLIBINC" $MAKE -f Makefile.test-zlib test-compile
		if [ $? -eq 0 ]; then
			echo "Found ZLIB include files in $ZLIBINC"
		else
			echo "ERROR: ZLIB include files found in $ZLIBINC, but compile fails."
			exit 1
		fi

		OS=`uname -s | tr '[/]' '[_]'` ZLIBLIB="-L$ZLIBLIB" $MAKE -f Makefile.test-zlib test-link
		if [ $? -eq 0 ]; then
			echo "Found ZLIB libraries in $ZLIBLIB"
			./test-zlib
		else
			echo "ERROR: ZLIB library files found in $ZLIBLIB, but link fails."
			exit 1
		fi

		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-zlib clean
		cd ..
	fi


