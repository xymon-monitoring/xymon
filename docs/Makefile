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

# install-safe places each file and directory without following a symlink at
# the destination name, owning and moding it as it goes. See client/Makefile.
INSTALLSAFE = ../build/install-safe
ifndef PKGBUILD
ISOWN = -o $(XYMONUSER) -g `$(IDTOOL) -g $(XYMONUSER)`
endif

install:
	# The help tree is created and each file placed per file -- no tar extract or
	# recursive chown, both of which would follow a symlink planted in the dir.
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages
	for f in manpages/*; do test -f "$$f" || continue; $(INSTALLSAFE) copy -m 644 $(ISOWN) "$$f" $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/`basename "$$f"` || exit 1; done
	for d in man1 man5 man7 man8; do \
		$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/$$d || exit 1; \
		for f in manpages/$$d/*; do test -e "$$f" || continue; $(INSTALLSAFE) copy -m 644 $(ISOWN) "$$f" $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/$$d/`basename "$$f"` || exit 1; done; \
	done
	for f in *html *txt *png *jpg; do test -e "$$f" || continue; $(INSTALLSAFE) copy -m 644 $(ISOWN) "$$f" $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/`basename "$$f"` || exit 1; done
	rm -f $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/man-index.html
	$(INSTALLSAFE) copy -m 644 $(ISOWN) man-index.html $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/index.html
