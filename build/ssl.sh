	echo "Checking for OpenSSL ..."

	OSSLINC=""
	OSSLLIB=""
	for DIR in /opt/openssl* /opt/ssl* /usr/local/openssl* /usr/local/ssl* /usr/local /usr/pkg /usr /opt/csw /opt/sfw/*ssl* /usr/sfw /usr/sfw/*ssl*
	do
		if test -d $DIR/include/openssl
		then
			OSSLINC=$DIR/include
		fi

		if test -f $DIR/lib/libcrypto.so
		then
			OSSLLIB=$DIR/lib
		fi
		if test -f $DIR/lib/libcrypto.a
		then
			OSSLLIB=$DIR/lib
		fi

		if test -f $DIR/lib64/libcrypto.so
		then
			OSSLLIB=$DIR/lib64
		fi
		if test -f $DIR/lib64/libcrypto.a
		then
			OSSLLIB=$DIR/lib64
		fi
	done

	if test "$USEROSSLINC" != ""; then
		OSSLINC="$USEROSSLINC"
	fi
	if test "$USEROSSLLIB" != ""; then
		OSSLLIB="$USEROSSLLIB"
	fi

	# Red Hat in their wisdom ships an openssl that depends on Kerberos,
	# and then puts the Kerberos includes where they are not found automatically.
	if test "`uname -s`" = "Linux" -a -d /usr/kerberos/include
	then
		OSSLINC="$OSSLINC -I/usr/kerberos/include"
	fi

	# Lets see if it builds
	SSLOK="YES"
	cd build
	if test "$OSSLINC" != ""; then INCOPT="-I$OSSLINC"; fi
	if test "$OSSLLIB" != ""; then LIBOPT="-L$OSSLLIB"; fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-ssl clean
	OS=`uname -s | sed -e's@/@_@g'` OSSLINC="$INCOPT" $MAKE -f Makefile.test-ssl test-compile
	if test $? -eq 0; then
		echo "Compiling with SSL library works OK"
	else
		echo "Warning: Cannot compile with SSL library"
		SSLOK="NO"
	fi

	OS=`uname -s | sed -e's@/@_@g'` OSSLLIB="$LIBOPT" $MAKE -f Makefile.test-ssl test-link
	if test $? -eq 0; then
		echo "Linking with SSL library works OK"
	else
		echo "Warning: Cannot link with SSL library"
		SSLOK="NO"
	fi
	OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-ssl clean
	cd ..

	if test "$SSLOK" = "YES"; then
		SSLFLAGS="-DHAVE_OPENSSL"

		cd build
		echo "Checking if your SSL library has SSLv2 enabled"
		OS=`uname -s | sed -e's@/@_@g'` OSSLINC="$INCOPT" OSSLLIB="$LIBOPT" $MAKE -f Makefile.test-ssl2 test-compile 2>/dev/null
		CSTAT=$?; LSTAT=$?
		if test $CSTAT -eq 0; then
			OS=`uname -s | sed -e's@/@_@g'` OSSLINC="$INCOPT" OSSLLIB="$LIBOPT" $MAKE -f Makefile.test-ssl2 test-link 2>/dev/null
			LSTAT=$?
		fi
		if test $CSTAT -ne 0 -o $LSTAT -ne 0; then
			echo "SSLv2 support disabled (dont worry, all systems should use SSLv1 or TLS)"
			OSSL2OK="N"
		else
			echo "Will support SSLv2 when testing SSL-enabled network services"
			OSSL2OK="Y"
			SSLFLAGS="$SSLFLAGS -DHAVE_SSLV2_SUPPORT"
		fi
		OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-ssl2 clean

		echo "Checking if your SSL library has SSLv3 enabled"
		OS=`uname -s | sed -e's@/@_@g'` OSSLINC="$INCOPT" OSSLLIB="$LIBOPT" $MAKE -f Makefile.test-ssl3 test-compile 2>/dev/null
		CSTAT=$?; LSTAT=$?
		if test $CSTAT -eq 0; then
			OS=`uname -s | sed -e's@/@_@g'` OSSLINC="$INCOPT" OSSLLIB="$LIBOPT" $MAKE -f Makefile.test-ssl3 test-link 2>/dev/null
			LSTAT=$?
		fi
		if test $CSTAT -ne 0 -o $LSTAT -ne 0; then
			echo "SSLv3 support disabled (dont worry, all systems should use SSLv1 or TLS)"
			OSSL3OK="N"
		else
			echo "Will support SSLv3 when testing SSL-enabled network services"
			OSSL3OK="Y"
			SSLFLAGS="$SSLFLAGS -DHAVE_SSLV3_SUPPORT"
		fi
		OS=`uname -s | sed -e's@/@_@g'` $MAKE -f Makefile.test-ssl3 clean
		cd ..
	fi

	if test "$SSLOK" = "NO"; then
		echo "OpenSSL include- or library-files not found."
		echo "Although you can use Xymon without OpenSSL, you will not"
		echo "be able to run network tests of SSL-enabled services, e.g. https."
		echo "So installing OpenSSL is recommended."
		echo "OpenSSL can be found at http://www.openssl.org/"
		echo ""
		echo "If you have OpenSSL installed, use the \"--sslinclude DIR\" and \"--ssllib DIR\""
		echo "options to configure to specify where they are."
		echo ""
		sleep 3
		echo "Continuing with SSL support disabled."
	fi

