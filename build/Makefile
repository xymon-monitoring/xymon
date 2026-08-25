TOOLS = merge-lines merge-sects setup-newfiles renamevars renametasks

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $< ../lib/libxymonclient.a

renamevars: renamevars.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

renametasks: renametasks.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

clean:
	rm -f $(TOOLS) testfile *.o *~

