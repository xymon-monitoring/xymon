/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This is a small library for bbgend worker modules, to read a new message   */
/* from the bbd_channel process, and also do the decoding of messages that    */
/* are passed on the "meta-data" first line of such a message.                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_worker.c,v 1.11 2004-10-27 10:47:30 henrik Exp $";

#include "bbdworker.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

static int didtimeout = 0;
static int ioerror = 0;

static char *readlntimed(char *buffer, size_t bufsize, struct timeval *timeout)
{
	static char stdinbuf[SHAREDBUFSZ+1];
	static int stdinbuflen = 0;
	struct timeval cutoff, now, tmo;
	struct timezone tz;
	char *eoln;
	fd_set fdread;

	if (ioerror) {
		errprintf("readlntimed: Will not read past an I/O error\n");
		return NULL;
	}

	/* Make sure the stdin buffer is null terminated */
	stdinbuf[stdinbuflen] = '\0';
	didtimeout = 0;
	eoln = NULL;

	if (timeout) {
		gettimeofday(&cutoff, &tz);
		cutoff.tv_sec += timeout->tv_sec;
		cutoff.tv_usec += timeout->tv_usec;
		if (cutoff.tv_usec > 1000000) {
			cutoff.tv_sec += 1;
			cutoff.tv_usec -= 1000000;
		}
	}

	/* Loop reading from stdin until we have a newline in the buffer, or we get a timeout */
	while (((eoln = strchr(stdinbuf, '\n')) == NULL) && !didtimeout && !ioerror) {
		int res;

		if (timeout) {
			gettimeofday(&now, &tz);
			tmo.tv_sec = cutoff.tv_sec - now.tv_sec;
			tmo.tv_usec = cutoff.tv_usec - now.tv_usec;
			if (tmo.tv_usec < 0) {
				tmo.tv_sec--;
				tmo.tv_usec += 1000000;
			}
		}

		FD_ZERO(&fdread);
		FD_SET(STDIN_FILENO, &fdread);

		res = select(STDIN_FILENO+1, &fdread, NULL, NULL, (timeout ? &tmo : NULL));

		if (res == -1) {
			/* Some error happened */
			if (errno != EINTR) {
				ioerror = 1;
			}
		}
		else if (res == 0) {
			/* Timeout - dont touch stdinbuflen */
			didtimeout = 1;
		}
		else if (FD_ISSET(STDIN_FILENO, &fdread)) {
			res = read(STDIN_FILENO, (stdinbuf+stdinbuflen), (sizeof(stdinbuf) - stdinbuflen - 1));
			if (res <= 0) {
				/* read() returns 0 --> End-of-file */
				ioerror = 1;
			}
			else {
				/* Got data */
				stdinbuflen += res;
			}
			stdinbuf[stdinbuflen] = '\0';
		}
		else {
			/* Cannot happen */
		}
	}

	if (eoln) {
		int n;

		/* Copy the first line over to the result buffer */
		n = (eoln - stdinbuf + 1);
		memcpy(buffer, stdinbuf, n);
		*(buffer + n) = '\0';
		stdinbuflen -= n;

		/* Now see if there is more data that we need to process later */
		eoln++; /* Skip past the newline */
		if (stdinbuflen) {
			/* Move the rest of the data to start of buffer */
			memmove(stdinbuf, eoln, stdinbuflen);
			stdinbuf[stdinbuflen] = '\0';
		}

		return buffer;
	}

	return NULL;
}


unsigned char *get_bbgend_message(char *id, int *seq, struct timeval *timeout)
{
	static unsigned int seqnum = 0;
	static unsigned char buf[SHAREDBUFSZ];
	static int bufsz = SHAREDBUFSZ;
	unsigned char *bufp;
	int buflen;
	int complete;
	char *p;

startagain:
	bufp = buf;
	buflen = 0;
	complete = 0;

	while (!complete) {
		if (readlntimed(bufp, (bufsz - buflen), timeout) == NULL) {
			if (didtimeout) {
				*seq = 0;
				strcpy(buf, "@@idle\n");
				return buf;
			}
			else {
				return NULL;
			}
		}

		if (strcmp(bufp, "@@\n") == 0) {
			/* "@@\n" marks the end of a multi-line message */
			bufp--; /* Backup over the final \n */
			complete = 1;
		}
		else if ((bufp == buf) && (strncmp(bufp, "@@", 2) != 0)) {
			/* A new message must begin with "@@" - if not, just drop those lines. */
			errprintf("%s: Out-of-sync data in channel: %s\n", id, bufp);
		}
		else {
			/* Add data to buffer */
			int n = strlen(bufp);
			buflen += n;
			bufp += n;
			if (buflen >= (bufsz-1)) {
				/* Buffer is full - force message complete */
				errprintf("%s: Buffer full, forcing message to complete\n", id);
				complete = 1;
			}
		}
	}

	/* Make sure buffer is NULL terminated */
	*bufp = '\0';

	p = buf + strcspn(buf, "0123456789|");
	if (isdigit(*p)) {
		*seq = atoi(p);

		if (debug) {
			p = strchr(buf, '\n'); if (p) *p = '\0';
			dprintf("%s: Got message %u %s\n", id, *seq, buf);
			if (p) *p = '\n';
		}

		if ((seqnum == 0) || (*seq == (seqnum + 1))) {
			/* First message, or the correct sequence # */
			seqnum = *seq;
		}
		else if (*seq == seqnum) {
			/* Duplicate message - drop it */
			errprintf("%s: Duplicate message %d dropped\n", id, *seq);
			goto startagain;
		}
		else {
			/* Out-of-sequence message. Cant do much except accept it */
			errprintf("%s: Got message %u, expected %u\n", id, *seq, seqnum+1);
			seqnum = *seq;
		}

		if (seqnum == 999999) seqnum = 0;
	}
	else {
		dprintf("%s: Got message with no serial\n", id);
		*seq = 0;
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

	if (msg == NULL) return;

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

