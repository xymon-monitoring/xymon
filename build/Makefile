TOOLS = merge-lines merge-sects setup-newfiles

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) -o $@ $(CFLAGS) $<

merge-sects: merge-sects.c
	$(CC) -o $@ $(CFLAGS) $<

setup-newfiles: setup-newfiles.c
	$(CC) -o $@ $(CFLAGS) $(RPATHOPT) $< ../lib/hobbitclient.a $(SSLLIBS) $(NETLIBS)

clean:
	rm -f $(TOOLS) testfile *.o *~

test-socklent: test-socklent.c
	$(CC) -c -o testfile.o $(CFLAGS) $<

test-vsnprintf: test-vsnprintf.c
	$(CC) -c -o testfile.o $(CFLAGS) $<

test-snprintf: test-snprintf.c
	$(CC) -c -o testfile.o $(CFLAGS) $<

test-rpcenth: test-rpcenth.c
	$(CC) -c -o testfile.o $(CFLAGS) $<

