all: demotool

demotool: demotool.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $< $(NETLIBS)

clean:
	rm -f *.o demotool *~

