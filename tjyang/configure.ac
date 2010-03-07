#----------------------------------------------------------------------------#
#     G N U    A U T O T O O L     S P E C I F I C A T I O N                 #
#----------------------------------------------------------------------------#
#                                                                            #
# NAME                                                                       # 
# configure.ac  used by bootstrap.sh                                         #
#                                                                            #
# REVISION HISTORY							     #
#     03/04/2010    T.J. Yang   				             #
#									     #
#                                                                            #
# USAGE								             #
#   bootstrap.sh to invoke configure.ac                                      #
# DESCRIPTION								     #
#   This autoconf script will create client and server "configure" script    #
#   it use automake to create Makefile from Makefile.am                      #
#   and libtool to create make create libraries xymon needed		     #
#                                                                            #
# RETURN CODE								     #
#   SUCCESS (=0) - function completed sucessfully			     #
#   ERROR   (=1) - error. 						     #
#   WARNING (=2) - warning... something's not quite right, but it's	     #
#                      not serious enough to prevent installation.	     #
#									     #
# LICENSE 								     #
#									     #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#									     #
#									     #
# COPYRIGHT                                                                  #
# Copyright (C) 2005-2008 Henrik Storner <henrik@hswn.dk>                    #
#									     #
#									     #
# ------------------------ HEADER  FILE DECLARATION -------------------------#
#									     #
#      checks for programs
#      checks for libraries
#      checks for header files
#      checks for types
#      checks for structures
#      checks for compiler characteristics
#      checks for library functions
#      checks for system services
#      AC_CONFIG_FILES([file...])
#      AC_OUTPUT
#       Autoconf requirements
AC_PREREQ([2.61])
#      AC_INIT(package, version, bug-report-address)
AC_INIT([xymon], [4.3.0], [tjyang2001@gmail.com])
# All the checking will insert into config.h
AC_CONFIG_HEADERS([config.h])
#      information on the package
#Clear scrren
clear
# issue a welcome message
AC_MSG_NOTICE([Configuring Xymon using GNU Autotool.])


## preserve any CFLAGS or LDFLAGS that may be set
## NOTE: This must be done before calling any macros that end up
## calling AC_PROG_CC or the like, since they will set a default
## set of CFLAGS ("-g -O2") if the user did not supply any, and
## we don't want those default flags to be carried over into the
## rest of the build system since we have other means of controlling
## debugging symbol generation and optimization.
#CONFIG_CFLAGS="${CFLAGS}"
#CONFIG_LDFLAGS="${LDFLAGS}"
#AC_SUBST(CONFIG_CFLAGS)
#AC_SUBST(CONFIG_LDFLAGS)



# Take augument of client or server.
AC_ARG_WITH(comp-mode,[ --with-comp-mode=<mode>
Specify a compilation mode: client or server],
[comp_mode=$withval],[comp_mode=client])

case "$comp_mode" in
    client)
    CFLAGS=-g 
    ;;
    server | * )
    CFLAGS=-O
    ;;
esac

# ------------------------ HEADER  FILE DECLARATION ------------------------- #
# 
AC_CONFIG_SRCDIR([lib/files.c])
AC_CONFIG_SRCDIR([client/clientupdate.c])
# ------------------------ HEADER  FILE DECLARATION ------------------------- #
AC_CONFIG_AUX_DIR([build-aux])
# Define a directory to hold M4 macros scripts.
AC_CONFIG_MACRO_DIR([m4])
# This is AutoMake macro, we pass on following error messages to automake.
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
#
# Checks for programs.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
# AC_PROG_RANLIB

# We want to use libtool for compiling and linking 


# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h strings.h sys/param.h sys/socket.h sys/time.h unistd.h utime.h])
#
# Checks for libraries.
# FIXME: Replace `main' with a function in `-lz':
AC_CHECK_LIB([z], [cos])
# FIXME: Replace `main' with a function in `-lm':
AC_CHECK_LIB([m], [cos])
# FIXME: Replace `main' with a function in `-lpcre':
AC_CHECK_LIB(pcre,pcre_free)
# FIXME: Replace `main' with a function in `-lrt':
AC_CHECK_LIB(rt,clock_gettime)
AC_CHECK_LIB(rrd,rrd_clear_error)


# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_FSEEKO
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_FUNC_WAIT3
AC_CHECK_FUNCS([alarm clock_gettime dup2 getcwd gethostbyname gethostname gettimeofday inet_ntoa memmove memset mkdir pathconf pstat_getdynamic putenv regcomp rmdir select socket strcasecmp strchr strcspn strdup strerror strncasecmp strrchr strspn strstr strtol utime])


if test "$ac_cv_header_rt_h" == yes
then
	AC_MSG_WARN([Great, We have library realtime for clock_gettime!])
fi
#
#

#AC_CONFIG_FILES([Makefile
#                 bbdisplay/Makefile
#                 bbnet/Makefile
#                 bbproxy/Makefile
#                 build/Makefile
#                 client/Makefile
#                 common/Makefile
#                 demotool/Makefile
#                 docs/Makefile
#                 hobbitd/Makefile
#                 lib/Makefile
#                 web/Makefile])
#
AC_CONFIG_FILES([Makefile 
lib/Makefile 
common/Makefile 
client/Makefile 
demotool/Makefile 
bbdisplay/Makefile 
bbproxy/Makefile 
docs/Makefile 
examples/Makefile 
hobbitd/Makefile])
# ------------------------ HEADER  FILE DECLARATION -------------------------#
# What: The last macro to generate all the above Makefiles.
AC_OUTPUT

if test "x${silent}" != "xyes" ; then

echo
echo "  #     #  #     #  #     #  #######  #     # "
echo "   #   #    #   #   ##   ##  #     #  ##    # "
echo "    # #      # #    # # # #  #     #  # #   # "
echo "     #        #     #  #  #  #     #  #  #  # "
echo "    # #       #     #     #  #     #  #   # # "
echo "   #   #      #     #     #  #     #  #    ## "
echo "  #     #     #     #     #  #######  #     # "
echo

fi

AC_MSG_NOTICE(Xymon source code configured for: )
AC_MSG_NOTICE( OS type  : $host_os)
AC_MSG_NOTICE( Host CPU : $host_cpu)
AC_MSG_NOTICE( build-cpu:vendor:os: $build_cpu : $build_vendor : $build_os :) 
AC_MSG_NOTICE( host-cpu:vendor:os: $host_cpu : $host_vendor : $host_os :) 






