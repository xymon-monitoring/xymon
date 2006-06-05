all:
	cat hobbit-tips.html.DIST | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' >hobbit-tips.html

clean:
	rm -f hobbit-tips.html *~

install:
	mkdir -p $(INSTALLROOT)$(INSTALLWWWDIR)/help/manpages
	cd manpages; tar cf - . | (cd $(INSTALLROOT)$(INSTALLWWWDIR)/help/manpages; tar xf -)
	cp -f *html *txt *png *jpg $(INSTALLROOT)$(INSTALLWWWDIR)/help/; rm -f $(INSTALLROOT)$(INSTALLWWWDIR)/help/man-index.html
	cp -f man-index.html $(INSTALLROOT)$(INSTALLWWWDIR)/help/manpages/index.html

