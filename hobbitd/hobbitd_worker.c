#include "bbdworker.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

unsigned char *get_bbgend_message(void)
{
	static unsigned int seqnum = 0;
	static unsigned char buf[SHAREDBUFSZ];
	unsigned char *bufp = buf;
	int bufsz = SHAREDBUFSZ;
	int buflen = 0;
	int complete = 0;
	char *p;

	while (!complete && fgets(bufp, (bufsz - buflen), stdin)) {
		if (strcmp(bufp, "@@\n") == 0) {
			/* "@@\n" marks the end of a multi-line message */
			bufp--; /* Backup over the final \n */
			complete = 1;
		}
		else if ((bufp == buf) && (strncmp(bufp, "@@", 2) != 0)) {
			/* A new message must begin with "@@" - if not, just drop those lines. */
			errprintf("Out-of-sync data in channel: %s\n", bufp);
		}
		else {
			/* Add data to buffer */
			int n = strlen(bufp);
			buflen += n;
			bufp += n;
		}
	}

	*bufp = '\0';

	dprintf("Got message\n");
	p = buf + strcspn(buf, "0123456789|");
	if (isdigit(*p)) {
		unsigned int seq = atoi(p);

		dprintf("Got message %u\n", seq);

		if ((seqnum == 0) || (seq == (seqnum + 1))) seqnum = seq;
		else {
			errprintf("Got message %u, expected %u\n", seq, seqnum+1);
			seqnum = seq;
		}
	}

	return ((!complete || (buflen == 0)) ? NULL : buf);
}

unsigned char *nlencode(unsigned char *msg)
{
	static unsigned char *buf = NULL;
	static int bufsz = 0;
	int maxneeded;
	unsigned char *inp, *outp;
	int n;

	if (msg == NULL) msg = "";

	maxneeded = 2*strlen(msg)+1;

	if (buf == NULL) {
		bufsz = maxneeded;
		buf = (char *)malloc(bufsz);
	}
	else if (bufsz < maxneeded) {
		bufsz = maxneeded;
		buf = (char *)realloc(buf, bufsz);
	}

	inp = msg;
	outp = buf;

	while (*inp) {
		n = strcspn(inp, "|\n\r\t\\");
		if (n > 0) {
			memcpy(outp, inp, n);
			outp += n;
			inp += n;
		}

		if (*inp) {
			*outp = '\\'; outp++;
			switch (*inp) {
			  case '|' : *outp = 'p'; outp++; break;
			  case '\n': *outp = 'n'; outp++; break;
			  case '\r': *outp = 'r'; outp++; break;
			  case '\t': *outp = 't'; outp++; break;
			  case '\\': *outp = '\\'; outp++; break;
			}
			inp++;
		}
	}
	*outp = '\0';

	return buf;
}

void nldecode(unsigned char *msg)
{
	unsigned char *inp = msg;
	unsigned char *outp = msg;
	int n;

	while (*inp) {
		n = strcspn(inp, "\\");
		if ((n > 0) && (inp != outp)) {
			memmove(outp, inp, n);
			inp += n;
			outp += n;
		}

		if (*inp == '\\') {
			inp++;
			switch (*inp) {
			  case 'p': *outp = '|';  outp++; inp++; break;
			  case 'r': *outp = '\r'; outp++; inp++; break;
			  case 'n': *outp = '\n'; outp++; inp++; break;
			  case 't': *outp = '\t'; outp++; inp++; break;
			  case '\\': *outp = '\\'; outp++; inp++; break;
			}
		}
		else if (*inp) {
			*outp = *inp;
			outp++; inp++;
		}
	}
	*outp = '\0';
}

