	echo "Checking for clock_gettime() requiring librt ..."

	LIBRTDEF=""

	cd build
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-clockgettime-librt clean
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-clockgettime-librt test-link 1>/dev/null 2>&1
	if [ $? -ne 0 ]; then
		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-clockgettime-librt test-link-rt 1>/dev/null 2>&1
		if [ $? -eq 0 ]; then
			echo "clock_gettime() requires librt"
			LIBRTDEF="-lrt"
		else
			echo "clock_gettime() not present, but this should be OK"
		fi

		OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-clockgettime-librt clean
	fi

	cd ..

