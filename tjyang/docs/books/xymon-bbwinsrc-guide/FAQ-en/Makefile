BODY	= faqbody.tex filectan.tex dirctan.tex
MACROS	= faq.cls faq.sty
CONFIGS	= archive.cfg
CMFONTS	= cmz
PATCH   = newfaq-patch.tex additions.tex add-errors.tex add-biblio.tex \
	  add-general.tex
MAKEF	= Makefile

LATEX	= latex
PDFLATEX = pdflatex
DVIPS	= dvips

# define variables WEB, WEB_BETA, CGI_BIN, HOME_DIR, CTAN_HOME, CTAN_ROOT
include locations.mk

CTAN_HOME = help/uk-tex-faq

#h
#hThe main targets of this Makefile are
#h	all		build a distribution
#h	patch		build latest patched version
#h
#h	ctan		install distribution on CTAN
#h			(to be run on the CTAN machine)
#h	web		install files required for web access
#h			(must be run on the machine that hosts
#h			the CGI script)
#h	web-beta	ditto, except beta distribution
#h
#h	inst-perl	install texfaq2html in the cgi-bin directory
#h			(to be run on the CTAN machine)
#h	inst-perl-beta	ditto, for texfaq2html-beta
#h
#h	html		make html files in html/
#h	inst-html	export html/ files via ftp.cl.cam.ac.uk

help:;	@sed -n 's/^#h//p' < $(MAKEF)

all:	newfaq.pdf letterfaq.pdf \
	newfaq.ps letterfaq.ps \
	newfaq-cm.dvi letterfaq-cm.dvi

# did have faqfont.cfg in here, but that caused problems, since it
# typically doesn't exist...

# pdf generation has to be done one a different base file name because
# of incompatibilities in the .aux and .toc files
newfaq.pdf: newfaq.tex $(BODY) $(MACROS) $(CONFIGS)
	ln -sf newfaq.tex ./pdf-newfaq.tex
	rm -f faqfont.cfg
	while ( \
	  $(PDFLATEX) \\def\\Status{1} \\input pdf-newfaq ; \
	  grep "Rerun to get cross" pdf-newfaq.log > /dev/null ) do true ; \
        done
	mv pdf-newfaq.pdf newfaq.pdf
	rm pdf-newfaq.tex

letterfaq.pdf: letterfaq.tex $(BODY) $(MACROS) $(CONFIGS)
	ln -sf letterfaq.tex ./pdf-letterfaq.tex
	rm -f faqfont.cfg
	while ( \
	  $(PDFLATEX) \\def\\Status{1} \\input pdf-letterfaq ; \
	  grep "Rerun to get cross" pdf-letterfaq.log > /dev/null ) do true ; \
        done
	mv pdf-letterfaq.pdf letterfaq.pdf
	rm pdf-letterfaq.tex

patch:	newfaq-patch

newfaq-patch: newfaq-patch.pdf newfaq-patch.ps

patch.pdf: newfaq-patch.pdf

newfaq-patch.pdf: $(PATCH) $(BODY) $(MACROS) $(CONFIGS)
	ln -sf newfaq-patch.tex ./pdf-newfaq-patch.tex
	rm -f faqfont.cfg
	while ( \
	  $(PDFLATEX) \\def\\Status{1} \\input pdf-newfaq-patch ; \
	  grep "Rerun to get cross" pdf-newfaq-patch.log > /dev/null ) do true ; \
        done
	mv pdf-newfaq-patch.pdf newfaq-patch.pdf
	rm pdf-newfaq-patch.tex

patch.ps: newfaq-patch.ps

newfaq-patch.ps: newfaq-patch.dvi
	$(DVIPS) -P$(CMFONTS) -j1 newfaq-patch

patch.dvi: newfaq-patch.dvi

newfaq-patch.dvi: $(PATCH) $(BODY) $(MACROS) $(CONFIGS)
	rm -f faqfont.cfg
	while ( \
	  $(LATEX) newfaq-patch ; \
	  grep "Rerun to get cross" newfaq-patch.log > /dev/null ) do true ; \
        done

newfaq.ps: newfaq.dvi
	$(DVIPS) -P$(CMFONTS) -j1 newfaq

letterfaq.ps: letterfaq.dvi
	$(DVIPS) -t letter -P$(CMFONTS) -j1 letterfaq

newfaq.dvi newfaq.aux: newfaq.tex $(BODY) $(MACROS) $(CONFIGS)
	while ( \
	  $(LATEX) newfaq ; \
	  grep "Rerun to get cross" newfaq.log > /dev/null ) do true ; \
        done

letterfaq.dvi: letterfaq.tex $(BODY) $(MACROS) $(CONFIGS)
	while ( \
	  $(LATEX) letterfaq ; \
	  grep "Rerun to get cross" letterfaq.log > /dev/null ) do true ; \
        done

newfaq-cm.dvi: newfaq.tex $(BODY) $(MACROS) $(CONFIGS)
	ln -sf faqfont.cfg.cmfonts faqfont.cfg
	ln -sf newfaq.tex newfaq-cm.tex
	while ( \
	  $(LATEX) newfaq-cm ; \
	  grep "Rerun to get cross" newfaq-cm.log > /dev/null ) do true ; \
        done
	rm faqfont.cfg newfaq-cm.tex

letterfaq-cm.dvi: letterfaq.tex $(BODY) $(MACROS) $(CONFIGS)
	ln -sf faqfont.cfg.cmfonts faqfont.cfg
	ln -sf letterfaq.tex letterfaq-cm.tex
	while ( \
	  $(LATEX) letterfaq-cm ; \
	  grep "Rerun to get cross" letterfaq-cm.log > /dev/null ) do true ; \
        done
	rm faqfont.cfg letterfaq-cm.tex

#install:
#	(cd /homes/rf/FTP/faq; \
#	 make -f /homes/rf/tex/faq/Makefile.FTP)

ctan:
	(cd $(CTAN_ROOT)/$(CTAN_HOME); \
	 really -u ctan make -f /homes/rf/tex/faq/Makefile.CTAN)

web: $(WEB)/dirctan.tex $(WEB)/filectan.tex $(WEB)/faqbody.tex $(WEB)/newfaq.aux $(WEB)/archive.list

$(WEB)/dirctan.tex: $(HOME_DIR)/dirctan.tex
$(WEB)/filectan.tex: $(HOME_DIR)/filectan.tex
$(WEB)/faqbody.tex: $(HOME_DIR)/faqbody.tex
$(WEB)/newfaq.aux: $(HOME_DIR)/newfaq.aux
$(WEB)/archive.list: $(HOME_DIR)/archive.list

$(WEB)/dirctan.tex $(WEB)/filectan.tex $(WEB)/faqbody.tex $(WEB)/newfaq.aux $(WEB)/archive.list:
	rm -f $@
	copy -t $? $@
	chmod 444 $@

web-beta: $(WEB_BETA)/dirctan.tex $(WEB_BETA)/filectan.tex $(WEB_BETA)/faqbody.tex $(WEB_BETA)/newfaq.aux $(WEB_BETA)/archive.list

$(WEB_BETA)/dirctan.tex: $(HOME_DIR)/dirctan.tex
$(WEB_BETA)/filectan.tex: $(HOME_DIR)/filectan.tex
$(WEB_BETA)/faqbody.tex: $(HOME_DIR)/faqbody.tex
$(WEB_BETA)/newfaq.aux: $(HOME_DIR)/newfaq.aux
$(WEB_BETA)/archive.list: $(HOME_DIR)/archive.list

$(WEB_BETA)/dirctan.tex $(WEB_BETA)/filectan.tex $(WEB_BETA)/faqbody.tex $(WEB_BETA)/newfaq.aux $(WEB_BETA)/archive.list:
	rm -f $@
	copy -t $? $@
	chmod 444 $@

inst-perl: $(CGI_BIN)/texfaq2html $(CGI_BIN)/sanitize.pl
inst-perl-beta: $(CGI_BIN)/texfaq2html-beta $(CGI_BIN)/sanitize-beta.pl

$(CGI_BIN)/texfaq2html: $(HOME_DIR)/texfaq2html
$(CGI_BIN)/texfaq2html-beta: $(HOME_DIR)/texfaq2html-beta
$(CGI_BIN)/sanitize.pl: $(HOME_DIR)/sanitize.pl
$(CGI_BIN)/sanitize-beta.pl: $(HOME_DIR)/sanitize.pl

$(CGI_BIN)/texfaq2html $(CGI_BIN)/texfaq2html-beta $(CGI_BIN)/sanitize.pl $(CGI_BIN)/sanitize-beta.pl:
	co -l $@
	copy -t $? $@
	ci -u -m"automatic check-in" $@

html:	$(HTMLDIR)/index.html

$(HTMLDIR)/index.html: $(BODY) newfaq.aux
	./texfaq2file

inst-html: $(HTMLDIR)/index.html
	gtar czvf /homes/rf/FTP/faq/FAQ-html.tar.gz README html/*

clean:
	rm -f *.ps *.pdf *.dvi *.log *.bak *.toc *.out comment.cut
	rm -f *.aux html/*.html
