	echo "Checking for OpenSSL ..."

	OSSLINC=""
	OSSLLIB=""
	for DIR in /usr/local/ssl /usr/local /usr
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
	done

	cd build
	OS=`uname -s` make -f Makefile.test-ssl clean
	OS=`uname -s` OSSLINC="-I$OSSLINC" make -f Makefile.test-ssl test-compile
	if [ $? -eq 0 ]; then
		echo "Found OpenSSL include files in $OSSLINC"
	else
		echo "WARNING: OpenSSL include files found in $OSSLINC, but compile fails."
	fi

	OS=`uname -s` OSSLLIB="-L$OSSLLIB" make -f Makefile.test-ssl test-link
	if [ $? -eq 0 ]; then
		echo "Found OpenSSL libraries in $OSSLLIB"
	else
		echo "WARNING: OpenSSL library files found in $OSSLLIB, but link fails."
	fi
	OS=`uname -s` make -f Makefile.test-ssl clean
	cd ..


