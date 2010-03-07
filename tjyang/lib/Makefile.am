# Files which are automatically distributed, if found:
#   ABOUT-GNU           README              config.rpath        ltcf-gcj.sh
#   ABOUT-NLS           THANKS              config.sub          ltconfig
#   AUTHORS             TODO                configure           ltmain.sh
#   BACKLOG             acconfig.h          configure.ac        mdate-sh
#   COPYING             aclocal.m4          configure.in        missing
#   COPYING.DOC         ansi2knr.1          depcomp             mkinstalldirs
#   COPYING.LESSER      ansi2knr.c          elisp-comp          py-compile
#   COPYING.LIB         compile             install-sh          stamp-vti
#   ChangeLog           config.guess        libversion.in       texinfo.tex
#   INSTALL             config.h.bot        ltcf-c.sh           ylwrap
#   NEWS                config.h.top        ltcf-cxx.sh
# 


# libgen.h
AM_CPPFLAGS = -I$(srcdir)/../include -I/usr/include/pcre
lib_LTLIBRARIES = libxymon.la libxymonclient.la
libxymon_la_SOURCES = say.c osdefs.c acklog.c availability.c calc.c cgi.c cgiurls.c clientlocal.c color.c  digest.c encoding.c environ.c errormsg.c eventlog.c files.c headfoot.c hobbitrrd.c holidays.c htmllog.c ipaccess.c loadalerts.c loadhosts.c loadnkconf.c locator.c links.c matching.c md5.c memory.c misc.c msort.c netservices.c notifylog.c rbtr.c reportlog.c rmd160c.c sendmsg.c sha1.c sha2.c sig.c stackio.c strfunc.c suid.c timefunc.c timing.c \
url.c compression.c

libxymon_la_SOURCES += say.h osdefs.h acklog.h availability.h calc.h cgi.h cgiurls.h clientlocal.h color.h  digest.h encoding.h environ.h errormsg.h eventlog.h files.h headfoot.h hobbitrrd.h holidays.h htmllog.h ipaccess.h loadalerts.h loadhosts.h loadnkconf.h locator.h links.h matching.h md5.h memory.h misc.h msort.h netservices.h notifylog.h rbtr.h reportlog.h rmd160c.h sendmsg.h sha1.h sha2.h sig.h stackio.h strfunc.h suid.h timefunc.h timing.h url.h compression.h


libxymonclient_la_SOURCES = osdefs.c cgiurls.c color.c digest.c encoding.c environ.c errormsg.c holidays.c ipaccess.c loadhosts.c md5.c memory.c misc.c msort.c rbtr.c rmd160c.c sendmsg.c sha1.c sha2.c sig.c stackio.c strfunc.c suid.c timefunc.c

libxymonclient_la_SOURCES += osdefs.h cgiurls.h color.h digest.h encoding.h environ.h errormsg.h holidays.h ipaccess.h loadhosts.h md5.h memory.h misc.h msort.h rbtr.h rmd160c.h sendmsg.h sha1.h sha2.h sig.h stackio.h strfunc.h suid.h timefunc.h


libxymon_la_LDFLAGS = -lz -lrt -lpcre -version-info 0:0:0 
libxymonclient_la_LDFLAGS = -lrt -lpcre -version-info 0:0:0 

# We need to addd libsocket when in OpenSoalris 
if ON_OPENSOLARIS
else
libxymon_la_LDFLAGS += -lsocket -lnsl
libxymonclient_la_LDFLAGS +=  -lsocket -lnsl
endif
