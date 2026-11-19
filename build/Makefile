TOOLS = merge-lines merge-sects setup-newfiles subst

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $< ../lib/libxymonclient.a

subst: subst.c
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) $<

clean:
	rm -f $(TOOLS) testfile *.o *~

