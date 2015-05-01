#!/bin/sh

# Simpler than autoconf, but it does what we need it to do right now.

echo "/* This file is auto-generated */" >include/config.h
echo "#ifndef __CONFIG_H__" >>include/config.h
echo "#define __CONFIG_H__ 1" >>include/config.h

echo "Checking for socklen_t"
$CC -c -o build/testfile.o $CFLAGS build/test-socklent.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_SOCKLEN_T 1" >>include/config.h
else
	echo "#undef HAVE_SOCKLEN_T" >>include/config.h
fi

echo "Checking for snprintf"
$CC -c -o build/testfile.o $CFLAGS build/test-snprintf.c 1>/dev/null 2>&1
if test $? -eq 0; then
	$CC -o build/testfile $CFLAGS build/testfile.o 1>/dev/null 2>&1
	if test $? -eq 0; then
		echo "#define HAVE_SNPRINTF 1" >>include/config.h
	else
		echo "#undef HAVE_SNPRINTF" >>include/config.h
	fi
else
	echo "#undef HAVE_SNPRINTF" >>include/config.h
fi

echo "Checking for vsnprintf"
$CC -c -o build/testfile.o $CFLAGS build/test-vsnprintf.c 1>/dev/null 2>&1
if test $? -eq 0; then
	$CC -o build/testfile $CFLAGS build/testfile.o 1>/dev/null 2>&1
	if test $? -eq 0; then
		echo "#define HAVE_VSNPRINTF 1" >>include/config.h
	else
		echo "#undef HAVE_VSNPRINTF" >>include/config.h
	fi
else
	echo "#undef HAVE_VSNPRINTF" >>include/config.h
fi

echo "Checking for rpc/rpcent.h"
$CC -c -o build/testfile.o $CFLAGS build/test-rpcenth.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_RPCENT_H 1" >>include/config.h
else
	echo "#undef HAVE_RPCENT_H" >>include/config.h
fi

echo "Checking for sys/select.h"
$CC -c -o build/testfile.o $CFLAGS build/test-sysselecth.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_SYS_SELECT_H 1" >>include/config.h
else
	echo "#undef HAVE_SYS_SELECT_H" >>include/config.h
fi

echo "Checking for u_int32_t typedef"
$CC -c -o build/testfile.o $CFLAGS build/test-uint.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_UINT32_TYPEDEF 1" >>include/config.h
else
	echo "#undef HAVE_UINT32_TYPEDEF" >>include/config.h
fi

echo "Checking for PATH_MAX definition"
$CC -o build/testfile $CFLAGS build/test-pathmax.c 1>/dev/null 2>&1
if test -x build/testfile; then ./build/testfile >>include/config.h; fi

echo "Checking for SHUT_RD/WR/RDWR definitions"
$CC -o build/testfile $CFLAGS build/test-shutdown.c 1>/dev/null 2>&1
if test -x build/testfile; then ./build/testfile >>include/config.h; fi

echo "Checking for strtoll()"
$CC -c -o build/testfile.o $CFLAGS build/test-strtoll.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_STRTOLL_H 1" >>include/config.h
else
	echo "#undef HAVE_STRTOLL_H" >>include/config.h
fi

echo "Checking for uname"
$CC -c -o build/testfile.o $CFLAGS build/test-uname.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_UNAME 1" >>include/config.h
else
	echo "#undef HAVE_UNAME" >>include/config.h
fi

# This is experimental for 4.3.x
#echo "Checking for POSIX binary tree functions"
#$CC -c -o build/testfile.o $CFLAGS build/test-bintree.c 1>/dev/null 2>&1
#if test $? -eq 0; then
#	echo "#define HAVE_BINARY_TREE 1" >>include/config.h
#else
	echo "#undef HAVE_BINARY_TREE" >>include/config.h
#fi



echo "#endif" >>include/config.h

echo "config.h created"
rm -f testfile.o testfile

exit 0

