	echo "Checking for lzo ..."

	LZOINC=""
	LZOLIB=""
	for DIR in /opt/lzo* /usr/local/lzo* /usr/local /usr/pkg /usr /opt/csw /opt/sfw
	do
		if test -f $DIR/include/lzo.h
		then
			LZOINC=$DIR/include
		fi
		if test -f $DIR/include/lzo/lzo.h
		then
			LZOINC=$DIR/include/lzo
		fi

		if test -f $DIR/lib/liblzo2.so
		then
			LZOLIB=$DIR/lib
		fi
		if test -f $DIR/lib/liblzo2.a
		then
			LZOLIB=$DIR/lib
		fi
		if test -f $DIR/lib64/liblzo2.so
		then
			LZOLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/liblzo2.a
		then
			LZOLIB=$DIR/lib64
		fi
	done

	if test "$USERLZOINC" != ""; then
		LZOINC="$USERLZOINC"
	fi
	if test "$USERLZOLIB" != ""; then
		LZOLIB="$USERLZOLIB"
	fi

	# Lets see if it can build
	LZOOK="YES"
	cd build
	if test ! -z $LZOINC; then INCOPT="-I$LZOINC"; fi
	if test ! -z $LZOLIB; then LIBOPT="-L$LZOLIB"; fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-lzo clean
	OS=`uname -s | sed -e's@/@_@g'` LZOINC="$INCOPT" $MAKE -f Makefile.test-lzo test-compile 2>/dev/null
	if test $? -eq 0; then
		echo "Compiling with LZO library works OK"
	else
		echo "ERROR: Cannot compile using LZO library."
		LZOOK="NO"
	fi

	OS=`uname -s | sed -e's@/@_@g'` LZOLIB="$LIBOPT" $MAKE -f Makefile.test-lzo test-link 2>/dev/null
	if test $? -eq 0; then
		echo "Linking with LZO library works OK"
	else
		echo "ERROR: Cannot link with LZO library."
		LZOOK="NO"
	fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-lzo clean
	cd ..

	if test "$LZOOK" = "NO"; then
		echo "Missing LZO include- or library-files. These are RECOMMENDED for xymond."
		echo "LZO can be found at http://www.oberhumer.com/opensource/lzo/"
		echo "If you have LZO installed, use the \"--lzoinclude DIR\" and \"--lzolib DIR\""
		echo "options to configure to specify where they are."
		echo ""
		echo "Using bundled minilzo files instead..."
		echo ""
		sleep 2
	fi

