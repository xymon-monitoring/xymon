all:
	cat hobbit-tips.html.DIST | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' >hobbit-tips.html

clean:
	rm -f hobbit-tips.html *~

install:
	mkdir -p $(INSTALLROOT)$(BBHOME)/www/help/manpages
	cd manpages; tar cf - . | (cd $(INSTALLROOT)$(BBHOME)/www/help/manpages; tar xf -)
	cp -f *html *txt *png $(INSTALLROOT)$(BBHOME)/www/help/; rm -f $(INSTALLROOT)$(BBHOME)/www/help/man-index.html
	cp -f man-index.html $(INSTALLROOT)$(BBHOME)/www/help/manpages/index.html

