	echo "Checking for Large File Support ..."

	# Solaris is br0ken when it comes to LFS tests.
	# See http://lists.xymon.com/archive/2011-November/033216.html
	if test "`uname -s`" = "SunOS"; then
		echo "Large File Support assumed OK on Solaris"
		exit 0
	fi


	cd build
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-lfs clean
	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-lfs 2>/dev/null
	if test $? -ne 0; then
		echo "ERROR: Compiler doesnt recognize the off_t C type."
		exit 1
	fi

	STDRES="`./test-lfs-std 4`"
	if test "$STDRES" != "4:1:0" -a "$STDRES" != "8:1:0"; then
		echo "ERROR: LFS support check failed for standard file support"
		exit 1
	fi

	LFSRES="`./test-lfs-lfs 8`"
	if test "$LFSRES" != "8:1:0"; then
		echo "ERROR: LFS support check failed for large file support"
		exit 1
	fi

	echo "Large File Support OK"

	OS=`uname -s | tr '[/]' '[_]'` $MAKE -f Makefile.test-lfs clean
	cd ..

