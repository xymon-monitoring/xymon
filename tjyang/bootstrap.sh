#!/bin/sh
#----------------------------------------------------------------------------#
#     G N U    A U T O T O O L     S P E C I F I C A T I O N                 #
#----------------------------------------------------------------------------#
#                                                                            #
# NAME                                                                       # 
# bootstrap.sh                                                               #
#                                                                            #
# REVISION HISTORY                                                           #
#     03/04/2010    T.J. Yang                                                #
#                                                                            #
#                                                                            #
# USAGE                                                                      #
#   "./bootstrap.sh" to invoke configure.ac                                  #
# DESCRIPTION                                                                #
#                                                                            #
# RETURN CODE                                                                #
#   SUCCESS (=0) - function completed sucessfully                            #
#   ERROR   (=1) - error.                                                    #
#   WARNING (=2) - warning... something's not quite right, but it's          #
#                      not serious enough to prevent installation.           #
#                                                                            #
# LICENSE                                                                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#                                                                            #
# COPYRIGHT                                                                  #
# Copyright (C) 2005-2008 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
#                                                                            #
# ------------------------ CONSTANTS  DECLARATION -------------------------#
SUCCESS=1
ERROR=1
EXIT_CODE=$SUCCESS

# ------------------------ SUB ROUTINES   -------------------------#

# WHAT:  run "$1 --version" to get the version number 
# WHY: To ensure you have correct version autotool installed on your build machine.
check_for_app() {
  $1 --version 2>&1 >/dev/null
  if [ $? != 0 ]
  then
    echo "Please install $1 and run bootstrap.sh again!"
    exit $ERROR
  fi
}

# WHAT: TO guess OS type
# WHY: so that we can generated help configure.ac to generate correct configure script.
check_OS() {
  if [ $? != 0 ]
  then
    echo "Please install $1 and run bootstrap.sh again!"
    exit $ERROR
  fi
}


# ------------------------ MAIN                -------------------------#
# On FreeBSD, multiple autoconf/automake versions have different names.
# On linux, envitonment variables tell which one to use.

# Check OS type
uname -s | grep -q FreeBSD
# 
if [ $? = 0 ] ; then	# FreeBSD case
	MY_AC_VER=259
	MY_AM_VER=19
else	# linux case
	MY_AC_VER=
	MY_AM_VER=
	AUTOCONF_VERSION=2.60
	AUTOMAKE_VERSION=1.9
	export AUTOCONF_VERSION
	export AUTOMAKE_VERSION
fi


check_for_app autoconf${MY_AC_VER}
check_for_app autoheader${MY_AC_VER}
check_for_app automake${MY_AM_VER}
check_for_app aclocal${MY_AM_VER}

echo "Generating the configure script ..."
# not need anymore, since autoreconf --install  do
# all the followings
#aclocal${MY_AM_VER} 2>/dev/null
#autoconf${MY_AC_VER}
#autoheader${MY_AC_VER}
#automake${MY_AM_VER} --add-missing --copy 2>/dev/null

autoreconf --install

if [ $? = 0 ] ; then	# FreeBSD case
 ./configure --prefix=/tmp/xymon 
fi

exit $SUCCESS
