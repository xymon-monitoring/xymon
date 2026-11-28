TOOLS = merge-lines merge-sects setup-newfiles install-safe

all: $(TOOLS)

install-safe: install-safe.c
	$(CC) $(LDFLAGS) -o $@ $(CPPFLAGS) $(CFLAGS) $<

merge-lines: merge-lines.c
	$(CC) $(LDFLAGS) -o $@ $(CPPFLAGS) $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) $(LDFLAGS) -o $@ $(CPPFLAGS) $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) $(LDFLAGS) -o $@ $(CPPFLAGS) $(CFLAGS) $< ../lib/libxymonclient.a

clean:
	rm -f $(TOOLS) testfile *.o *~

