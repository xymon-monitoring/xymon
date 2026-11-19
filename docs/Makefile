# The Apache alias for the web pages, as xymond/Makefile writes it for
# xymon-apache.conf: a literal Alias, so the URL is never read as a regular
# expression; at the root ("/" to configure leaves XYMONHOSTURL empty) it
# needs the trailing slash on both sides.
XYMONWEBALIAS = $(if $(XYMONHOSTURL),Alias $(XYMONHOSTURL) "$(INSTALLWWWDIR)",Alias / "$(INSTALLWWWDIR)/")

# build/Makefile.rules sets this; the default here is for a "make -C" of
# this directory alone, which would otherwise install the web content at /.
INSTALLSTATICWWWDIR ?= $(INSTALLWWWDIR)

all: xymon-tips.html xymon-apacheconf.txt

xymon-tips.html: xymon-tips.html.DIST
	../build/subst XYMONHOSTURL='$(XYMONHOSTURL)' <$< >$@

xymon-apacheconf.txt: xymon-apacheconf.txt.DIST
	../build/subst INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' INSTALLETCDIR='$(INSTALLETCDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' XYMONWEBALIAS='$(XYMONWEBALIAS)' <$< >$@

clean:
	rm -f xymon-tips.html xymon-apacheconf.txt *~

install:
	mkdir -p $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages
	cd manpages; tar cf - . | (cd $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages; tar xf -)
	cp -f *html *txt *png *jpg $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/; rm -f $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/man-index.html
	cp -f man-index.html $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/index.html
ifndef PKGBUILD
	chown -R $(XYMONUSER) $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help
	chgrp -R `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help
	# These may fail if no files installed
	(find $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/ -type d | xargs chmod 755 ) || /bin/true
	(find $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/ -type f | xargs chmod 644 ) || /bin/true
endif
