all: demotool

demotool: demotool.o
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(NETLIBS)

clean:
	rm -f *.o demotool *~

