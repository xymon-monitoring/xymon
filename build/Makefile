TOOLS = merge-lines merge-sects setup-newfiles renamevars renametasks

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) -o $@ $(CFLAGS) $(RPATHOPT) $< ../lib/xymonclient.a $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

renamevars: renamevars.c
	$(CC) -o $@ $(CFLAGS) $<

renametasks: renametasks.c
	$(CC) -o $@ $(CFLAGS) $<

clean:
	rm -f $(TOOLS) testfile *.o *~

