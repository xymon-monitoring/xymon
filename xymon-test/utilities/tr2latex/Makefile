# GNU Makefile for tr2latex on *NIX machines

# CC chooses your C compiler
CC = gcc

# PREFIX sets your base directory
PREFIX = /usr/local
#PREFIX = /opt/tr2latex

# TEXDIR gives the path where the tex library resides (fonts, macros ...)
TEXDIR = $(PREFIX)/share/texmf-local
#TEXDIR = $(PREFIX)/texlive/texmf-local

# flags for compiling C sources
CFLAGS += -Wall -O2

##############################################################################
# nothing to configure beyond this point

CFILES = tr2latex.c tr.c subs.c
HFILES = protos.h setups.h simil.h greek.h macros.h maths.h flip.h forbid.h

tr2latex: $(CFILES:%.c=%.o)
	$(CC) -o $@ $^

tr2latex.o: tr2latex.c setups.h protos.h
tr.o: tr.c setups.h protos.h
subs.o: subs.c $(HFILES)
version.o: version.c

install: tr2latex
	install -m 755 -d $(PREFIX)/bin
	install -m 755 -d $(PREFIX)/share/man/man1
	install -m 755 -d $(TEXDIR)/macros
	install -m 0755 tr2latex $(PREFIX)/bin/tr2latex
	install -m 0444 tr2latex.man $(PREFIX)/share/man/man1/tr2latex.1
	install -m 0444 troffman.sty troffms.sty $(TEXDIR)/macros

uninstall:
	rm -f $(PREFIX)/bin/tr2latex $(PREFIX)/share/man/man1/tr2latex.1 $(TEXDIR)/macros/troffman.sty $(TEXDIR)/macros/troffms.sty

clean:
	-rm -f *.o core tr2latex *~ TAGS tags
