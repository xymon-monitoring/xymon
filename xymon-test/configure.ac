#----------------------------------------------------------------------------#
#     G N U    A U T O T O O L     S P E C I F I C A T I O N                 #
#----------------------------------------------------------------------------#
#                                                                            #
# NAME                                                                       # 
# configure.ac  used by autogen.sh                                           #
#                                                                            #
# REVISION HISTORY                                                           #
#     03/04/2010    T.J. Yang                                                #
#                                                                            #
#                                                                            #
# USAGE                                                                      #
#   autogen.sh to invoke configure.ac                                        #
# DESCRIPTION                                                                #
#   This autoconf script will create "configure" that accept --client and    #
#   --server options like the original man-made configure script.            #
#   it use automake to create Makefile from Makefile.am                      #
#   and libtool to create make create libraries xymon needed                 #
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
#                                                                            #
#                                                                            #
# ------------------------ HEADER  FILE DECLARATION -------------------------#

#      Autoconf requirements
AC_PREREQ([2.60])
#      AC_INIT(package, version, bug-report-address)
AC_INIT([xymon], [4.3.0], [tjyang2001@gmail.com])

AC_COPYRIGHT("Xymon")
# Need to specify options of what kind of clients to configured for.
# 1. LOCALCLIENT that need to build pcre software to process 
#                local messages and only send in result. 
# 2.
# 3. SERVER

# MYARGS=0
# if test $# == $MYARGS; then
#  AC_MSG_ERROR([Use --help to see options to configure xymon client or server])
# fi

#
# Take augument of client or server.
AC_ARG_WITH(compile-mode,[--with-compile-mode=<mode> Xymon compilation mode: client or server],
[compile_mode=$withval],[compile_mode=client])

case "$compile_mode" in
    client)
    CFLAGS=-g 
    ;;
    server | * )
    CFLAGS=-O
    ;;
esac
#
#VER=$(VERSION)
#AC_SUBST([VER]) 

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


# preserve any CFLAGS or LDFLAGS that may be set
# NOTE: This must be done before calling any macros that end up
# calling AC_PROG_CC or the like, since they will set a default
# set of CFLAGS ("-g -O2") if the user did not supply any, and
# we don't want those default flags to be carried over into the
# rest of the build system since we have other means of controlling
# debugging symbol generation and optimization.
CONFIG_CFLAGS="${CFLAGS}"
CONFIG_LDFLAGS="${LDFLAGS}"
AC_SUBST(CONFIG_CFLAGS)
AC_SUBST(CONFIG_LDFLAGS)
# Define some constants for C definsive programming.
AC_DEFINE([VER],[4.3.0],[Define Constant SUCCESS = 0 ])
AC_DEFINE([SUCCESS],[0],[Define Constant SUCCESS = 0 ])
AC_DEFINE([ERROR],[1],[Define Constant SUCCESS = 1 ])
AC_DEFINE([WARNING],[-2],[Define Constant WARNING = -2 ])
AC_DEFINE([LOCALCLIENT], [1], [Xymon local client: it will require pcre library to process the xymon message locally.])
AC_DEFINE([XYMONCLIENT], 1, [ONLY Configured for Xymon Client.])
AC_DEFINE([XYMONSERVER], 1, [ONLY Configured for Xymon Server.])


# All the checking will insert into config.h
AC_CONFIG_HEADERS([config.h])
#      information on the package
#Clear scrren
# clear
# issue a welcome message
AC_MSG_NOTICE([Configuring Xymon using GNU Autotool for ${host_os}])

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

# ------------------------ PRE CHECK  ------------------------- #
#AC_CHECK_PROGS([TAR],[tar gtar],[:])
#if test "$TAR" = ":"  
#then
#   AC_MSG_ERROR([Xymon needs tar]}
#fi
# ------------------------ HEADER  FILE DECLARATION ------------------------- #
# 
AC_CONFIG_SRCDIR([lib/files.c])
AC_CONFIG_SRCDIR([client/clientupdate.c])
# ------------------------ HEADER  FILE DECLARATION ------------------------- #
AC_CONFIG_AUX_DIR([build-aux])
# Define a directory to hold M4 macros scripts.

# Opensoalris doesn't have autopoint in it's gettext implementation.
#
#AM_GNU_GETTEXT_VERSION([0.17])
#AM_GNU_GETTEXT([external])
#
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
AC_PROG_RANLIB

# We want to use libtool for compiling and linking 


# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h \
limits.h netdb.h netinet/in.h stdint.h \
stdlib.h string.h strings.h sys/param.h \
sys/socket.h sys/time.h unistd.h utime.h \
snprintf.h vsnprintf.h])

AC_CHECK_HEADERS([sys/select.h])
AC_CHECK_LIB([c], [snprintf])
AC_CHECK_LIB([c], [vsnprintf])
# Checks for libraries.
# FIXME: Replace `main' with a function in `-lz':
AC_CHECK_LIB([z], [inflate])
  
# dnl ************************************************** 
# dnl * zlib is neeeded for handling compressed xymon messages.
# dnl ************************************************** 
# AC_CHECK_LIB(z, inflate, 
#              [Z_LIBS="-lz"; have_zlib=yes], 
#              have_zlib=no) 
# if test "x$have_zlib" != xyes; then 
#     AC_CHECK_LIB(zlib, inflate, 
#                  [Z_LIBS="-lzlib"; have_zlib=yes], 
#                  have_zlib=no) 
# fi 
# if test x"$have_zlib" = xyes; then 
#    AH_TEMPLATE([HAVE_ZLIB], [Whether zlib is installed, it's used for compressed iTunesCDB]) 
#    AC_DEFINE_UNQUOTED(HAVE_ZLIB, 1) 
# fi 
# AC_SUBST(Z_LIBS) 
# AM_CONDITIONAL(HAVE_ZLIB, test x"$have_zlib" = xyes) 
# 

AC_CHECK_LIB([m], [cos])
AC_CHECK_LIB(pcre,pcre_free)
AC_CHECK_LIB(rt,clock_gettime)
AC_CHECK_LIB(rrd,rrd_clear_error)

# check opensolaris
AC_CHECK_HEADER([/usr/include/pcre/pcre.h],[on_opensolaris=no])
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
AC_CHECK_FUNCS([alarm clock_gettime \
dup2 getcwd gethostbyname gethostname \
gettimeofday inet_ntoa memmove memset \
mkdir pathconf pstat_getdynamic putenv \
regcomp rmdir select socket strcasecmp \
strchr strcspn strdup strerror strncasecmp \ 
strrchr strspn strstr strtol utime])


if test "$ac_cv_header_rt_h" == yes
then
    AC_MSG_WARN([Great, We have library realtime for clock_gettime!])
fi
# 
AM_CONDITIONAL(ON_DARWIN, [test "x-${host_os}" = "x-darwin10.2.0"])
AM_CONDITIONAL(ON_OPENSOLARIS, [test "x-${host_os}" = "x-solaris2.11"])
#
case "${host_os}" in
  freebsd*)
    ac_default_prefix=/usr/local
    CPPFLAGS=-I/usr/local/include
    LDFLAGS=-L/usr/local/lib
    ;;
  linux-gnu*)
    ac_default_prefix=/tmp/xymon
    if test ${prefix} = '/usr/local' || test ${prefix} = 'NONE'; then
      if test ${sysconfdir} = '${prefix}/etc'; then
         sysconfdir=/etc
      fi
      if test ${mandir} = '${prefix}/man'; then
         mandir=/usr/share/man
      fi
    fi
    CPPFLAGS=-I/usr/local/include
    LDFLAGS=-L/tmp/xymon/lib
    ;;
  darwin10.2.0)
    CPPFLAGS=-I/opt/local/include
    LDFLAGS=-L/opt/local/lib 
    ;;
  *)
    ac_default_prefix=/usr
    if test ${prefix} = '/usr' || test ${prefix} = 'NONE'; then
       if test ${sysconfdir} = '${prefix}/etc'; then
          sysconfdir=/etc
       fi
       if test ${mandir} = '${prefix}/man'; then
          mandir=/usr/share/man
       fi
    fi
  ;;
esac 


AC_CONFIG_FILES([Makefile 
lib/Makefile 
common/Makefile 
client/Makefile 
tools/demotool/Makefile 
bbdisplay/Makefile 
bbproxy/Makefile 
docs/Makefile 
tools/tr2latex/Makefile 
examples/Makefile 
hobbitd/Makefile
package-sources/rpm/hobbit.spec:package-sources/rpm/hobbit.spec.in
client/clientversion.cfg:client/clientversion.cfg.in
doxygen/doxyfile.xymon.conf:doxygen/doxyfile.xymon.in
])



# ------------------------ HEADER  FILE DECLARATION -------------------------#
# What: The last macro to generate all the above Makefiles.
AC_OUTPUT

if test "x${silent}" != "xyes" ; then
echo ${host_os}
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

AC_MSG_NOTICE(Xymon source code configured as $CLIENT_TYPE for: )
AC_MSG_NOTICE( OS type  : $host_os)
AC_MSG_NOTICE( Host CPU : $host_cpu)
AC_MSG_NOTICE( build-cpu:vendor:os: $build_cpu : $build_vendor : $build_os :) 
AC_MSG_NOTICE( host-cpu:vendor:os: $host_cpu : $host_vendor : $host_os :) 


