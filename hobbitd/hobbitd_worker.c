#include "bbdworker.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

char *msg_data(char *msg)
{
	/* Find the start position of the data following the "status host.test " message */
	char *result;
	
	result = strchr(msg, '.');		/* Hits the '.' in "host.test" */
	if (!result) {
		dprintf("Msg was not what I expected: '%s'\n", msg);
		return msg;
	}

	result += strcspn(result, " \t\n");	/* Skip anything until we see a space, TAB or NL */
	result += strspn(result, " \t");	/* Skip all whitespace */

	return result;
}

unsigned char *get_bbgend_message(void)
{
	static unsigned char buf[4*MAXMSG];
	unsigned char *bufp = buf;
	int bufsz = 4*MAXMSG;
	int buflen = 0;
	int complete = 0;

	while (!complete && fgets(bufp, (bufsz - buflen), stdin)) {
		if (strcmp(bufp, "@@\n") == 0) {
			/* "@@\n" marks the end of a multi-line message */
			bufp--; /* Backup over the final \n */
			complete = 1;
		}
		else if ((bufp == buf) && (strncmp(bufp, "@@", 2) != 0)) {
			/* A new message must begin with "@@" - if not, just drop those lines. */
			dprintf("Out-of-sync data in channel: %s\n", bufp);
		}
		else {
			/* Add data to buffer */
			int n = strlen(bufp);
			buflen += n;
			bufp += n;
		}
	}

	*bufp = '\0';
	return ((!complete || (buflen == 0)) ? NULL : buf);
}

