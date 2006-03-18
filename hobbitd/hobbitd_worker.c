/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is a small library for hobbitd worker modules, to read a new message  */
/* from the hobbitd_channel process, and also do the decoding of messages     */
/* that are passed on the "meta-data" first line of such a message.           */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_worker.c,v 1.23 2006-03-18 07:33:02 henrik Exp $";

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ..  . */
#endif

#include <sys/time.h>
#include <time.h>
#include <errno.h>

#include "libbbgen.h"

#include "hobbitd_ipc.h"
#include "hobbitd_worker.h"


unsigned char *get_hobbitd_message(enum msgchannels_t chnid, char *id, int *seq, struct timeval *timeout)
{
	static unsigned int seqnum = 0;
	static char *idlemsg = NULL;
	static char *buf = NULL;
	static size_t bufsz = 0;
	static char *startpos;	/* Where our unused data starts */
	static char *endpos;	/* Where the first message ends */
	static char *fillpos;	/* Where our unused data ends (the \0 byte) */
	static char *movebuf = NULL; /* Result buffer - used only when we need to shuffle data around */
	static int ioerror = 0;

	struct timeval cutoff;
	struct timezone tz;
	int needmoredata;
	char *endsrch;		/* Where in the buffer do we start looking for the end-message marker */
	char *result;

	/*
	 * The way this works is to read data from stdin into a
	 * buffer. Each read fetches as much data as possible,
	 * i.e. all that is available up to the amount of 
	 * buffer space we have.
	 *
	 * When the buffer contains a complete message,
	 * we return a pointer to the message.
	 *
	 * Since a read into the buffer can potentially
	 * fetch multiple messages, we need to keep track of
	 * the start/end positions of the next message, and
	 * where in the buffer new data should be read in.
	 * As long as there is a complete message available
	 * in the buffer, we just return that message - only
	 * when there is no complete message do we read data
	 * from stdin.
	 *
	 * A message is normally NOT copied, we just return
	 * a pointer to our input buffer. The only time we 
	 * need to shuffle data around is if the buffer
	 * does not have room left to hold a complete message.
	 * In that case, we use the "movebuf" to hold the
	 * current result message, and move the remainder of
	 * data (the incomplete next message) to the start of
	 * the buffer.
	 */

startagain:
	if (ioerror) return NULL;

	if (movebuf) {
		/* 
		 * movebuf is used to return a message sometimes. So
		 * we free it when it has been used.
		 */
		xfree(movebuf);
		movebuf = NULL;
	}

	if (buf == NULL) {
		/*
		 * Initial setup of the buffers.
		 * We allocate a buffer large enough for the largest message
		 * that can arrive on this channel, and add 4KB extra room.
		 * The extra 4KB is to allow the memmove() that will be
		 * needed occasionally some room to work optimally.
		 */
		bufsz = shbufsz(chnid) + 4096;
		buf = (char *)malloc(bufsz+1);
		*buf = '\0';
		startpos = fillpos = buf;
		endpos = NULL;

		/* idlemsg is used to return the idle message in case of timeouts. */
		idlemsg = strdup("@@idle\n");
	}

	if (timeout) {
		/* Calculate when the read should timeout. */
		gettimeofday(&cutoff, &tz);
		cutoff.tv_sec += timeout->tv_sec;
		cutoff.tv_usec += timeout->tv_usec;
		if (cutoff.tv_usec > 1000000) {
			cutoff.tv_sec += 1;
			cutoff.tv_usec -= 1000000;
		}
	}

	/*
	 * Start looking for the end-of-message marker at the beginning of
	 * the message. The next scans will only look at the new data we've
	 * got when reading data in.
	 */
	endsrch = startpos;

	/* We only need to read data, if we do not have an end-of-message marker */
	needmoredata = (endpos == NULL);
	while (needmoredata) {
		/* Fill buffer with more data until we get an end-of-message marker */
		struct timeval now, tmo;
		fd_set fdread;
		int res;

		if (timeout) {
			/* How long time until the timeout ? */
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

		if (res < 0) {
			/* Some error happened */
			if (errno != EINTR) {
				ioerror = 1;
				return NULL;
			}
		}
		else if (res == 0) {
			/* 
			 * Timeout - return the "idle" message.
			 * NB: If select() was not passed a timeout parameter, this cannot trigger
			 */
			*seq = 0;
			return idlemsg;
		}
		else if (FD_ISSET(STDIN_FILENO, &fdread)) {
			size_t bufleft = bufsz - (fillpos - buf);
			res = read(STDIN_FILENO, fillpos, bufleft);
			if (res <= 0) {
				/* read() returns 0 --> End-of-file */
				ioerror = 1;
				return NULL;
			}
			else {
				/* 
				 * Got data - null-terminate it, and update fillpos
				 */
				*(fillpos+res) = '\0';
				fillpos += res;

				/* Did we get an end-of-message marker ? Then we're done. */
				endpos = strstr(endsrch, "\n@@\n");
				needmoredata = (endpos == NULL);

				/*
				 * If not done, update endsrch. We need to look at the
				 * last 3 bytes of input we got - they could be "\n@@" so
				 * all that is missing is the final "\n".
				 */
				if (needmoredata && (res >= 3)) endsrch = fillpos-3;
			}
		}
	}

	/* We have a complete message between startpos and endpos */
	result = startpos;
	*endpos = '\0';
	startpos = endpos+4;
	endpos = strstr(startpos, "\n@@\n");

	if ((endpos == NULL) && ((bufsz - (startpos - buf)) < (bufsz/2))) {
		size_t usedbytes = (fillpos - startpos);

		movebuf = strdup(result);
		result = movebuf;
		memmove(buf, startpos, usedbytes);
		startpos = buf;
		fillpos = startpos + usedbytes;
		*fillpos = '\0';
	}

	/* Get and check the message sequence number */
	{
		char *p = result + strcspn(result, "0123456789|");
		if (isdigit((int)*p)) {
			*seq = atoi(p);

			if (debug) {
				p = strchr(result, '\n'); if (p) *p = '\0';
				dprintf("%s: Got message %u %s\n", id, *seq, result);
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
	}

	return result;
}

