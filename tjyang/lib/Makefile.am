# libgen.h
AM_CPPFLAGS = -I$(srcdir)/../include -I/usr/include/pcre
lib_LTLIBRARIES = libxymon.la libxymonclient.la
libxymon_la_SOURCES = say.c say.h osdefs.c acklog.c availability.c calc.c cgi.c cgiurls.c clientlocal.c color.c  digest.c encoding.c environ.c errormsg.c eventlog.c files.c headfoot.c hobbitrrd.c holidays.c htmllog.c ipaccess.c loadalerts.c loadhosts.c loadnkconf.c locator.c links.c matching.c md5.c memory.c misc.c msort.c netservices.c notifylog.c rbtr.c reportlog.c rmd160c.c sendmsg.c sha1.c sha2.c sig.c stackio.c strfunc.c suid.c timefunc.c timing.c url.c osdefs.h acklog.h availability.h calc.h cgi.h cgiurls.h clientlocal.h color.h  digest.h encoding.h environ.h errormsg.h eventlog.h files.h headfoot.h hobbitrrd.h holidays.h htmllog.h ipaccess.h loadalerts.h loadhosts.h loadnkconf.h locator.h links.h matching.h md5.h memory.h misc.h msort.h netservices.h notifylog.h rbtr.h reportlog.h rmd160c.h sendmsg.h sha1.h sha2.h sig.h stackio.h strfunc.h suid.h timefunc.h timing.h url.h compression.c

libxymonclient_la_SOURCES = osdefs.c cgiurls.c color.c digest.c encoding.c environ.c errormsg.c holidays.c ipaccess.c loadhosts.c md5.c memory.c misc.c msort.c rbtr.c rmd160c.c sendmsg.c sha1.c sha2.c sig.c stackio.c strfunc.c suid.c timefunc.c

libxymon_la_LDFLAGS = -lz -lrt -lpcre -version-info 0:0:0 
libxymonclient_la_LDFLAGS = -lrt -lpcre -version-info 0:0:0 

# We need to addd libsocket when in OpenSoalris 
if ON_OPENSOLARIS
libxymon_la_LDFLAGS += -lsocket -lnsl
libxymonclient_la_LDFLAGS +=  -lsocket -lnsl
endif
