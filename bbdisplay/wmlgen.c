#include <stdlib.h>
#include <stdio.h>

#include "bbgen.h"
#include "wmlgen.h"

void do_wml_cards(void)
{
	FILE		*fd;
	char		fn[256];
	hostlist_t	*h;

	sprintf(fn, "%s/bb-hosts-wml.tmp", getenv("BBTMP"));
	fd = fopen(fn, "w");
	if (fd == NULL) {
		printf("Cannot open %s\n", fn);
		return;
	}

	for (h = hosthead; (h); h = h->next) {
		if (h->hostentry->color == COL_RED) {
			fprintf(fd, "%s %s %s\n", 
				h->hostentry->ip, h->hostentry->hostname,
				h->hostentry->rawentry);
		}
	}

	fclose(fd);
}

