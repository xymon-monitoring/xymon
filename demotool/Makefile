all: demotool

demotool: demotool.o
	$(CC) $(CFLAGS) -o $@ $< $(NETLIBS)

clean:
	rm -f *.o demotool *~

