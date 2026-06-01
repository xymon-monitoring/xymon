	echo "Checking for C-ARES library ..."

	CARESINC=""
	CARESLIB=""
	for DIR in /opt/c*ares* /usr/local/c*ares* /usr/local /usr/pkg /opt/csw /opt/sfw /opt/homebrew /opt/local
	do
		if test -f $DIR/include/ares.h
		then
			CARESINC=$DIR/include
		fi
		if test -f $DIR/include/ares/ares.h
		then
			CARESINC=$DIR/include/ares
		fi

		if test -f $DIR/lib/libcares.so
		then
			CARESLIB=$DIR/lib
		fi
		if test -f $DIR/lib/libcares.dylib
		then
			CARESLIB=$DIR/lib
		fi
		if test -f $DIR/lib/libcares.a
		then
			CARESLIB=$DIR/lib
		fi
		if test -f $DIR/lib64/libcares.so
		then
			CARESLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/libcares.a
		then
			CARESLIB=$DIR/lib64
		fi
	done

	if test "$USERCARESINC" != ""; then
		CARESINC="$USERCARESINC"
	fi
	if test "$USERCARESLIB" != ""; then
		CARESLIB="$USERCARESLIB"
	fi

	# Lets see if it can build
	CARESOK="YES"
	cd build
	if test "$CARESINC" != ""; then INCOPT="-I$CARESINC"; fi
	if test "$CARESLIB" != ""; then LIBOPT="-L$CARESLIB"; fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-cares ares-clean
	OS=`uname -s | sed -e's@/@_@g'` CARESINC="$INCOPT" $MAKE -f Makefile.test-cares test-compile
	if test $? -eq 0; then
		echo "Compiling with c-ares library works OK"
	else
		echo "ERROR: Cannot compile using c-ares library."
		CARESOK="NO"
	fi

	if test "$CARESOK" = "YES"
	then
		OS=`uname -s | sed -e's@/@_@g'` CARESLIB="$LIBOPT" $MAKE -f Makefile.test-cares test-link
		if test $? -eq 0; then
			echo "Linking with c-ares library works OK"
		else
			echo "ERROR: Cannot link with c-ares library."
			CARESOK="NO"
		fi
		OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-cares ares-clean
	fi
	cd ..

	if test "$CARESOK" = "NO"; then
		echo "The c-ares library (async DNS resolver) is missing or not usable."
		echo "It is REQUIRED to build xymonnet. Install the development package"
		echo "(e.g. libc-ares-dev / c-ares-devel / c-ares), or, if it is in a"
		echo "non-standard location, use the \"--caresinclude DIR\" and \"--careslib DIR\""
		echo "options to configure to specify where it is."
		echo "c-ares can be found at https://c-ares.org/"
		exit 1
	fi


