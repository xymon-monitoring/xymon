TOOLS = merge-lines merge-sects

all: $(TOOLS)

merge-lines: merge-lines.c
	$(CC) -o merge-lines $(CFLAGS) merge-lines.c

merge-sects: merge-sects.c
	$(CC) -o merge-sects $(CFLAGS) merge-sects.c

clean:
	rm -f $(TOOLS) *.o *~
