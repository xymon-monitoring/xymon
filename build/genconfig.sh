#!/bin/sh

# Simpler than autoconf, but it does what we need it to do right now.

echo "/* This file is auto-generated */" >include/config.h

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
	echo "#define HAVE_SNPRINTF 1" >>include/config.h
else
	echo "#undef HAVE_SNPRINTF" >>include/config.h
fi

echo "Checking for vsnprintf"
$CC -c -o build/testfile.o $CFLAGS build/test-vsnprintf.c 1>/dev/null 2>&1
if test $? -eq 0; then
	echo "#define HAVE_VSNPRINTF 1" >>include/config.h
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

echo "config.h created"
exit 0

