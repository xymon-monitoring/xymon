# Xymon library Makefile
#

XYMONLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o crondate.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o xymonrrd.o holidays.o htmllog.o ipaccess.o loadalerts.o loadcriticalconf.o links.o matching.o md5.o memory.o misc.o msort.o netservices.o notifylog.o acknowledgementslog.o readmib.o reportlog.o rmd160c.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc.o tree.o url.o webaccess.o

XYMONCOMMLIBOBJS = $(XYMONLIBOBJS) loadhosts.o locator.o sendmsg.o xymond_ipc.o xymond_buffer.o
XYMONTIMELIBOBJS = run.o timing.o

CLIENTLIBOBJS = osdefs.o cgiurls.o color-client.o crondate.o digest.o encoding.o environ-client.o errormsg.o holidays.o ipaccess.o md5.o memory.o misc.o msort.o rmd160c.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc-client.o tree.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif
XYMONCLIENTCOMMLIBOBJS = locator.o loadhosts.o sendmsg.o xymond_ipc.o xymond_buffer.o

XYMONCLIENTLIB = libxymonclient
XYMONCLIENTLIBS = -lxymonclient
XYMONCLIENTCOMMLIB = libxymonclientcomm
XYMONCLIENTCOMMLIBS = -lxymonclientcomm $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

XYMONLIB = libxymon
XYMONLIBS = -lxymon
XYMONCOMMLIB = libxymoncomm
XYMONCOMMLIBS = -lxymoncomm $(NETLIBS) $(LIBRTDEF) $(ZLIBLIBS) $(SSLLIBS) $(PCRELIBS)
XYMONTIMELIB = libxymontime
XYMONTIMELIBS = -lxymontime $(LIBRTDEF)


CFLAGS += -I../include -L../lib
ifeq ($(ENABLESHARED),yes)
	CFLAGS += -fpic
	# SONAME = ".0"
	# LIBVERSION = ".0.0.0"
endif

# NOTE - shared build is a prototype, not production-ready.
# See xymon-monitoring/xymon#436 for the design decision. Gaps:
#   - SONAME/LIBVERSION are stubbed out above, so this builds an
#     unversioned libxymon.so with no soname: acceptable for a private
#     library rebuilt as a unit, wrong for a public/SDK one.
#   - the binaries get no rpath to the installed libxymon.so (RPATHVAL
#     carries only the external deps), so a --shared install is not
#     loadable until the runtime libdir is added to it.
#   - -shared is hardcoded here (Linux/GNU only); other platforms need
#     their own flags.
# --shared therefore stays off by default until #436 settles direction.

all: test-endianness $(XYMONLIB) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB) xymonloadhosts xymonstackio xymonavailability md5 sha1 rmd160 xymonlocator tree

client: test-endianness $(XYMONCLIENTLIB) $(XYMONCLIENTCOMMLIB) $(XYMONTIMELIB)

install:
	cp -fp *.a $(INSTALLROOT)$(INSTALLLIBDIR)/
	chmod 644 $(INSTALLROOT)$(INSTALLLIBDIR)/*.a
	# Shared objects only if this tree built any: devel does, main does
	# not, and an unmatched glob must not look like a failure.
	for f in *.so*; do \
		test -e "$$f" || continue; \
		cp -fp "$$f" $(INSTALLROOT)$(INSTALLLIBDIR)/ || exit 1; \
		chmod 755 $(INSTALLROOT)$(INSTALLLIBDIR)/"$$f" || exit 1; \
	done
	# The headers, so the libraries above can actually be built against.
	# include/ and lib/ have to stay siblings: libxymon.h reaches its
	# remaining headers as "../lib/...", so a flat directory breaks on
	# the first include. One -I$(INSTALLINCDIR)/include then works.
	mkdir -p $(INSTALLROOT)$(INSTALLINCDIR)/include $(INSTALLROOT)$(INSTALLINCDIR)/lib
	cp -fp ../include/*.h $(INSTALLROOT)$(INSTALLINCDIR)/include/
	cp -fp *.h $(INSTALLROOT)$(INSTALLINCDIR)/lib/
	chmod 644 $(INSTALLROOT)$(INSTALLINCDIR)/include/*.h $(INSTALLROOT)$(INSTALLINCDIR)/lib/*.h

test-endianness: test-endianness.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

$(XYMONLIB): $(XYMONLIBOBJS)
	ar cr $(XYMONLIB).a $(XYMONLIBOBJS)
	ranlib $(XYMONLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONLIB).so$(SONAME) -o $(XYMONLIB).so$(LIBVERSION) $(XYMONLIBOBJS) || :

$(XYMONCOMMLIB): $(XYMONCOMMLIBOBJS)
	ar cr $(XYMONCOMMLIB).a $(XYMONCOMMLIBOBJS)
	ranlib $(XYMONCOMMLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONCOMMLIB).so$(SONAME) -o $(XYMONCOMMLIB).so$(LIBVERSION) $(XYMONCOMMLIBOBJS) || :

$(XYMONTIMELIB): $(XYMONTIMELIBOBJS)
	ar cr $(XYMONTIMELIB).a $(XYMONTIMELIBOBJS)
	ranlib $(XYMONTIMELIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONTIMELIB).so$(SONAME) -o $(XYMONTIMELIB).so$(LIBVERSION) $(XYMONTIMELIBOBJS) || :

$(XYMONCLIENTLIB): $(CLIENTLIBOBJS)
	ar cr $(XYMONCLIENTLIB).a $(CLIENTLIBOBJS)
	ranlib $(XYMONCLIENTLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONCLIENTLIB).so$(SONAME) -o $(XYMONCLIENTLIB).so$(LIBVERSION) $(CLIENTLIBOBJS) || :

$(XYMONCLIENTCOMMLIB): $(XYMONCLIENTCOMMLIBOBJS)
	ar cr $(XYMONCLIENTCOMMLIB).a $(XYMONCLIENTCOMMLIBOBJS)
	ranlib $(XYMONCLIENTCOMMLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONCLIENTCOMMLIB).so$(SONAME) -o $(XYMONCLIENTCOMMLIB).so$(LIBVERSION) $(XYMONCLIENTCOMMLIBOBJS) || :

loadhosts.o: loadhosts.c loadhosts_file.c loadhosts_net.c
	$(CC) $(CFLAGS) -c -o $@ loadhosts.c

eventlog.o: eventlog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

notifylog.o: notifylog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

acknowledgementslog.o: acknowledgementslog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

headfoot.o: headfoot.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

loadalerts.o: loadalerts.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

matching.o: matching.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

sha1.o: sha1.c test-endianness
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

rmd160c.o: rmd160c.c test-endianness
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

environ.o: environ.c
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONHOME)\" -c -o $@ environ.c

environ-client.o: environ.c
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONCLIENTHOME)\" -c -o $@ environ.c

color-client.o: color.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

timefunc-client.o: timefunc.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

xymonloadhosts: loadhosts.c loadhosts_file.c loadhosts_net.c $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c $(XYMONCOMMLIBS)

xymonstackio: stackio.c $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE -o $@ stackio.c $(XYMONLIBS)

xymonavailability: availability.c $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE -o $@ availability.c $(XYMONLIBS)

md5: md5.c test-endianness
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ md5.c

sha1: sha1.c test-endianness
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c

rmd160: rmd160c.c test-endianness
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ rmd160c.c

xymonlocator: locator.c $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE -o $@ locator.c $(XYMONCOMMLIBS) $(XYMONLIBS)

tree: tree.c
	$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE -o $@ tree.c

# The diagnostic programs built by "all" are otherwise discarded with the
# build tree: nothing installs them, so anyone who wants xymonstackio to
# check how a config file parses, or xymonloadhosts to see what hosts.cfg
# resolves to, has to keep a build tree around or copy them out by hand. Every
# packaging that ships them does the latter.
#
# Deliberately not part of "install": these are diagnostics, and whether an
# installation gets them is for whoever runs the install to decide.
# Packagers and anyone who wants them can ask:
#
#	make install-tools
#
# tree is built but not installed: it is an interactive test of the xtree
# code that reads only its own stdin, so it shows nothing about an
# installation, and tests/libxymon/xtree-*.sh already cover that code.
#
# They are built under the names they are installed by -- xymon<name>,
# not the source file's bare name: names like locator are too generic for
# a directory a packager may put on PATH.
#
# Their one manual page, xymontools.1, is installed here too and not by an
# install-man, so a plain "make install" puts no page in man1 for programs
# it did not install. Its HTML copy does ship in the web help, like every
# page, and says that install-tools installs them.
LIBTOOLS = xymonloadhosts xymonstackio xymonavailability xymonlocator

install-tools: $(LIBTOOLS)
	@mkdir -p $(INSTALLROOT)$(INSTALLBINDIR)
	for f in $(LIBTOOLS); do \
		cp -fp $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; \
		chmod 755 $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; \
	done
	@mkdir -p $(INSTALLROOT)$(MANROOT)/man1
	cp -fp xymontools.1 $(INSTALLROOT)$(MANROOT)/man1/
	chmod 644 $(INSTALLROOT)$(MANROOT)/man1/xymontools.1
ifndef PKGBUILD
	for f in $(LIBTOOLS); do \
		chown $(XYMONUSER) $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; \
		chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; \
	done
	chown $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1/xymontools.1
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1/xymontools.1
endif

clean:
	rm -f *.o *.a *~ $(LIBTOOLS) test-endianness md5 sha1 rmd160 tree
