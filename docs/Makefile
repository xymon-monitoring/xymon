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
	XYMONHOSTURL='$(XYMONHOSTURL)' ../build/subst.sh XYMONHOSTURL <$< >$@

xymon-apacheconf.txt: xymon-apacheconf.txt.DIST
	INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' INSTALLETCDIR='$(INSTALLETCDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' XYMONWEBALIAS='$(XYMONWEBALIAS)' ../build/subst.sh INSTALLWWWDIR CGIDIR SECURECGIDIR INSTALLETCDIR XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL XYMONWEBALIAS <$< >$@

clean:
	rm -f xymon-tips.html xymon-apacheconf.txt *~

install:
	mkdir -p $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages
	cd manpages; tar cf - . | (cd $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages; tar xf -)
	# help/ belongs to $(XYMONUSER): remove each name before copying so a symlink
	# there is not followed and written through.
	for h in *html *txt *png *jpg; do rm -f $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/$$h; done
	cp -f *html *txt *png *jpg $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/; rm -f $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/man-index.html
	rm -f $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/index.html
	cp -f man-index.html $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/index.html
ifndef PKGBUILD
	find $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help -exec chown -h $(XYMONUSER) {} \;
	find $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help -exec chgrp -h `$(IDTOOL) -g $(XYMONUSER)` {} \;
	# These may fail if no files installed
	(find $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/ -type d | xargs chmod 755 ) || /bin/true
	(find $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/ -type f | xargs chmod 644 ) || /bin/true
endif
