	echo "Checking for Large File Support ..."

	cd build
	OS=`uname -s` $MAKE -f Makefile.test-lfs clean
	OS=`uname -s` $MAKE -f Makefile.test-lfs 2>/dev/null
	if [ $? -ne 0 ]; then
		echo "ERROR: Compiler doesnt recognize the off_t C type."
		exit 1
	fi

	if test "`./test-lfs-std 4`" != "1:0"; then
		echo "ERROR: LFS support check failed for standard file support"
		exit 1
	fi

	if test "`./test-lfs-lfs 8`" != "1:0"; then
		echo "ERROR: LFS support check failed for large file support"
		exit 1
	fi

	echo "Large File Support OK"

	OS=`uname -s` $MAKE -f Makefile.test-lfs clean
	cd ..

