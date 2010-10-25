TOOLS = merge-lines merge-sects setup-newfiles

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) -o $@ $(CFLAGS) $(RPATHOPT) $< ../lib/xymonclient.a $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

clean:
	rm -f $(TOOLS) testfile *.o *~

