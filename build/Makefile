TOOLS = merge-lines merge-sects setup-newfiles

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) -o $@ $(CFLAGS) $<

clean:
	rm -f $(TOOLS) *.o *~
