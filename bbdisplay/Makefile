# Makefile for bbgen - bbdisplay programs
#


PROGRAMS = bbgen bb-rep.cgi bb-replog.cgi bb-hist.cgi bb-snapshot.cgi bb-eventlog.cgi bb-findhost.cgi bb-csvinfo.cgi bb-hostsvc.cgi bb-ack.cgi bb-webpage

EXTENSIONS = bbcombotest

CGISCRIPTS = bb-rep.sh bb-replog.sh bb-hist.sh bb-snapshot.sh bb-eventlog.sh bb-findhost.sh bb-csvinfo.sh bbmessage.cgi bb-hostsvc.sh bb-histlog.sh
ifdef BBGEND
	CGISCRIPTS += bb-ack.sh
endif

TEMPLATES = acknowledge_header acknowledge_footer acknowledge_form findhost_header findhost_footer findhost_form report_header report_footer report_form

GENOBJS = bbgen.o loadhosts.o loaddata.o reportdata.o bbconvert.o larrdgen.o pagegen.o eventlog.o acklog.o process.o infogen.o savelog.o wmlgen.o rssgen.o bb-replog.o util.o debug.o
COMBOTESTOBJS = bbcombotest.o
REPOBJS = bb-rep.o
REPLOGOBJS = bb-replog-cgi.o reportdata.o
HISTOBJS = bb-hist.o reportdata.o
SNAPOBJS = bb-snapshot.o
EVENTLOGOBJS = bb-eventlog.o util.o			# util.o for histlogurl, find_or_create_column
FINDHOSTOBJS = bb-findhost.o loadhosts.o util.o		# util.o for hostpage_name, hostpage_link, findlink, hosthead
CSVINFOOBJS = bb-csvinfo.o
MESSAGECGIOBJS = sendmsg-cgi.o
HOSTSVCCGIOBJS = bb-hostsvc.o
ACKCGIOBJS = bb-ack.o
WEBPAGEOBJS = bb-webpage.o

HTMLPAGES = bb-findhost.html
GIFFILES = bbfindhost1.gif bbfindhost2.gif


all: $(PROGRAMS) $(EXTENSIONS) $(CGISCRIPTS)


bbgen: $(GENOBJS)
	$(CC) $(CFLAGS) -o $@ $(GENOBJS) ../lib/libbbgen.a $(NETLIBS)

bbcombotest: $(COMBOTESTOBJS)
	$(CC) $(CFLAGS) -o $@ $(COMBOTESTOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-rep.cgi: $(REPOBJS)
	$(CC) $(CFLAGS) -o $@ $(REPOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-replog.cgi: $(REPLOGOBJS)
	$(CC) $(CFLAGS) -o $@ $(REPLOGOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-hist.cgi: $(HISTOBJS)
	$(CC) $(CFLAGS) -o $@ $(HISTOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-snapshot.cgi: $(SNAPOBJS)
	$(CC) $(CFLAGS) -o $@ $(SNAPOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-eventlog.cgi: $(EVENTLOGOBJS)
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-findhost.cgi: $(FINDHOSTOBJS)
	$(CC) $(CFLAGS) -o $@ $(FINDHOSTOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-csvinfo.cgi: $(CSVINFOOBJS)
	$(CC) $(CFLAGS) -o $@ $(CSVINFOOBJS) ../lib/libbbgen.a $(NETLIBS)

bbmessage.cgi: $(MESSAGECGIOBJS)
	$(CC) $(CFLAGS) -o $@ $(MESSAGECGIOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-hostsvc.cgi: $(HOSTSVCCGIOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOSTSVCCGIOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-ack.cgi: $(ACKCGIOBJS)
	$(CC) $(CFLAGS) -o $@ $(ACKCGIOBJS) ../lib/libbbgen.a $(NETLIBS)

bb-webpage: $(WEBPAGEOBJS)
	$(CC) $(CFLAGS) -o $@ $(WEBPAGEOBJS) ../lib/libbbgen.a


################################################
# Objects requiring special compile flags 
################################################
digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

bb-hist.o: bb-hist.c
	$(CC) $(CFLAGS) $(HISTGRAPHDEF) -c -o $@ bb-hist.c


################################################
# Objects compiled differently for use in 
# bbgen/bbtest-net, and in standalone programs
################################################
bb-eventlog.o: eventlog.o
	$(CC) $(CFLAGS) -DCGI -c -o $@ eventlog.c

bb-replog-cgi.o: bb-replog.c
	$(CC) $(CFLAGS) -DCGI -c -o $@ bb-replog.c

sendmsg-cgi.o: ../lib/sendmsg.c
	$(CC) $(CFLAGS) -DCGI -c -o $@ ../lib/sendmsg.c

bb-hostsvc.o: bb-hostsvc.c
	$(CC) $(CFLAGS) -DCGI -c -o $@ bb-hostsvc.c

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

ifdef BBGEND
%.sh: %.sh-bbgend.DIST
	echo BBGEND is $(BBGEND)
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' >$@
	chmod 755 $@
else
%.sh: %.sh-bbd.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' >$@
	chmod 755 $@
endif

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(EXTENSIONS) $(CGISCRIPTS)


install: install-nocgi install-cgi
install-nocgi: install-bin install-exts install-html install-gif install-templates install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-exts: $(EXTENSIONS)
	cp -fp $(EXTENSIONS) $(BBHOME)/ext/

install-cgi: $(CGISCRIPTS)
	@echo ""
	@echo "  WARNING : About to overwrite any existing Big Brother CGI scripts"
	@echo "            Hit ctrl-C NOW if you want to save them first!"
	@echo ""
	@sleep 5
	cp -fp $(CGISCRIPTS) $(CGIDIR)/

install-html: $(HTMLPAGES)
	cp -fp $(HTMLPAGES) $(BBHOME)/www/help/

install-gif: $(GIFFILES)
	BBHOME=$(BBHOME) ../build/install-nooverwrite.sh www/gifs $(GIFFILES)

install-templates: $(TEMPLATES)
	BBHOME=$(BBHOME) ../build/install-nooverwrite.sh web $(TEMPLATES)

install-man:
	mkdir -p $(MANROOT)/man{1,5}
	cp -fp *.1 $(MANROOT)/man1/
	cp -fp *.5 $(MANROOT)/man5/

