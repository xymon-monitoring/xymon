TOOLS = merge-lines merge-sects setup-newfiles

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $< ../lib/libxymonclient.a

clean:
	rm -f $(TOOLS) testfile *.o *~

