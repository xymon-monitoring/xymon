	echo "Checking for lz4 ..."

	LZ4INC=""
	LZ4LIB=""
	for DIR in /opt/lz4* /usr/local/lz4* /usr/local /usr/pkg /usr /opt/csw /opt/sfw
	do
		if test -f $DIR/include/lz4.h
		then
			LZ4INC=$DIR/include
		fi
		if test -f $DIR/include/lz4/lz4.h
		then
			LZ4INC=$DIR/include/lz4
		fi

		if test -f $DIR/lib/liblz4.so
		then
			LZ4LIB=$DIR/lib
		fi
		if test -f $DIR/lib/liblz4.a
		then
			LZ4LIB=$DIR/lib
		fi
		if test -f $DIR/lib64/liblz4.so
		then
			LZ4LIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/liblz4.a
		then
			LZ4LIB=$DIR/lib64
		fi
	done

	if test "$USERLZ4INC" != ""; then
		LZ4INC="$USERLZ4INC"
	fi
	if test "$USERLZ4LIB" != ""; then
		LZ4LIB="$USERLZ4LIB"
	fi

	# Lets see if it can build
	LZ4OK="YES"
	cd build
	if test ! -z $LZ4INC; then INCOPT="-I$LZ4INC"; fi
	if test ! -z $LZ4LIB; then LIBOPT="-L$LZ4LIB"; fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-lz4 clean
	OS=`uname -s | sed -e's@/@_@g'` LZ4INC="$INCOPT" $MAKE -f Makefile.test-lz4 test-compile 2>/dev/null
	if test $? -eq 0; then
		echo "Compiling with LZ4 library works OK"
	else
		echo "ERROR: Cannot compile using LZ4 library."
		LZ4OK="NO"
	fi

	OS=`uname -s | sed -e's@/@_@g'` LZ4LIB="$LIBOPT" $MAKE -f Makefile.test-lz4 test-link 2>/dev/null
	if test $? -eq 0; then
		echo "Linking with LZ4 library works OK"
	else
		echo "ERROR: Cannot link with LZ4 library."
		LZ4OK="NO"
	fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-lz4 clean
	cd ..

	if test "$LZ4OK" = "NO"; then
		echo "Missing LZ4 include- or library-files. These are suggested for busy xymond servers."
		echo "LZ4 can be found at https://github.com/Cyan4973/lz4/ (or in EPEL or some other package repository)"
		echo "If you have LZ4 installed, use the \"--lz4include DIR\" and \"--lz4lib DIR\""
		echo "options to configure to specify where they are."
		echo ""
		echo "Continuing without LZ4 support..."
		echo ""
		sleep 2
	fi

