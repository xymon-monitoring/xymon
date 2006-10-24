/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This is a small library for hobbitd worker modules, to read a new message  */
/* from the hobbitd_channel process, and also do the decoding of messages     */
/* that are passed on the "meta-data" first line of such a message.           */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_worker.c,v 1.28 2006-10-24 15:12:00 henrik Exp $";

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ..  . */
#endif

#include <sys/time.h>
#include <time.h>
#include <errno.h>

#include "libbbgen.h"

#include "hobbitd_ipc.h"
#include "hobbitd_worker.h"


#define EXTRABUFSPACE 4095

unsigned char *get_hobbitd_message(enum msgchannels_t chnid, char *id, int *seq, struct timeval *timeout, int *terminated)
{
	static unsigned int seqnum = 0;
	static char *idlemsg = NULL;
	static char *buf = NULL;
	static size_t bufsz = 0;
	static size_t maxmsgsize = 0;
	static int ioerror = 0;

	static char *startpos;	/* Where our unused data starts */
	static char *endpos;	/* Where the first message ends */
	static char *fillpos;	/* Where our unused data ends (the \0 byte) */

	struct timeval cutoff;
	struct timezone tz;
	int maymove, needmoredata;
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
	 */

	if (buf == NULL) {
		/*
		 * Initial setup of the buffers.
		 * We allocate a buffer large enough for the largest message
		 * that can arrive on this channel, and add 4KB extra room.
		 * The EXTRABUFSPACE is to allow the memmove() that will be
		 * needed occasionally some room to work optimally.
		 */
		maxmsgsize = 1024*shbufsz(chnid);
		bufsz = maxmsgsize + EXTRABUFSPACE;
		buf = (char *)malloc(bufsz+1);
		*buf = '\0';
		startpos = fillpos = buf;
		endpos = NULL;

		/* idlemsg is used to return the idle message in case of timeouts. */
		idlemsg = strdup("@@idle\n");

		/* We dont want to block when reading data. */
		fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
	}

	/*
	 * If the start of the next message doesn't begin with "@@" then 
	 * there's something rotten.
	 * It might be some data left-over from an oversized message.
	 */
	if (*startpos && strncmp(startpos, "@@", 2) != 0) {
		errprintf("Bad data in channel, skipping it\n");
		startpos = strstr(startpos, "\n@@");
		endpos = (startpos ? strstr(startpos, "\n@@\n") : NULL);
		if (startpos && (startpos == endpos)) {
			startpos = endpos + 4;
			endpos = strstr(startpos, "\n@@\n");
		}

		if (!startpos) {
			startpos = buf;
			fillpos = buf;
			endpos = NULL;
		}

		seqnum = 0; /* After skipping, we dont know what to expect */
	}

startagain:
	if (ioerror) {
		errprintf("get_hobbitd_message: Returning NULL due to previous i/o error\n");
		return NULL;
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

	/*
	 * See if the current available buffer space is enough to hold a full message.
	 * If not, then flag that we may do a memmove() of the buffer data.
	 */
	maymove = ((startpos + maxmsgsize) >= (buf + bufsz));

	/* We only need to read data, if we do not have an end-of-message marker */
	needmoredata = (endpos == NULL);
	while (needmoredata) {
		/* Fill buffer with more data until we get an end-of-message marker */
		struct timeval now, tmo;
		fd_set fdread;
		int res;
		size_t bufleft = bufsz - (fillpos - buf);
		size_t usedbytes = (fillpos - startpos);

		dbgprintf("Want msg %d, startpos %ld, fillpos %ld, endpos %ld, usedbytes=%ld, bufleft=%ld\n",
			  (seqnum+1), (startpos-buf), (fillpos-buf), (endpos ? (endpos-buf) : -1), usedbytes, bufleft);

		if (usedbytes >= maxmsgsize) {
			/* Over-size message. Truncate it. */
			errprintf("Got over-size message, truncating at %d bytes (max: %d)\n", usedbytes, maxmsgsize);
			endpos = startpos + maxmsgsize;
			memcpy(endpos, "\n@@\n", 4);	/* Simulate end-of-message and flush data */
			needmoredata = 0;
			continue;
		}

		if (maymove && (bufleft < EXTRABUFSPACE)) {
			/* Buffer is almost full - move data to accomodate a large message. */
			dbgprintf("Moving %d bytes to start of buffer\n", usedbytes);
			memmove(buf, startpos, usedbytes);
			startpos = buf;
			fillpos = startpos + usedbytes;
			*fillpos = '\0';
			endsrch = (usedbytes >= 4) ? (fillpos - 4) : startpos;
			maymove = 0;
			bufleft = bufsz - (fillpos - buf);
		}

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
			if (*terminated) return NULL;
			if ((errno == EAGAIN) || (errno == EINTR)) continue;

			/* Some error happened */
			ioerror = 1;
			dbgprintf("get_hobbitd_message: Returning NULL due to select error %s\n",
				  strerror(errno));
			return NULL;
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
			res = read(STDIN_FILENO, fillpos, bufleft);
			if (res < 0) {
				if ((errno == EAGAIN) || (errno == EINTR)) continue;

				ioerror = 1;
				dbgprintf("get_hobbitd_message: Returning NULL due to read error %s\n",
					  strerror(errno));
				return NULL;
			}
			else if (res == 0) {
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
	startpos = endpos+4; /* +4 because we skip the "\n@@\n" end-marker from the previous message */
	endpos = strstr(startpos, "\n@@\n");	/* To see if we already have a full message loaded */
	/* fillpos stays where it is */

	/* Check that it really is a message, and not just some garbled data */
	if (strncmp(result, "@@", 2) != 0) {
		errprintf("Dropping (more) garbled data\n");
		goto startagain;
	}

	/* Get and check the message sequence number */
	{
		char *p = result + strcspn(result, "0123456789|");
		if (isdigit((int)*p)) {
			*seq = atoi(p);

			if (debug) {
				p = strchr(result, '\n'); if (p) *p = '\0';
				dbgprintf("%s: Got message %u %s\n", id, *seq, result);
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

	dbgprintf("startpos %ld, fillpos %ld, endpos %ld\n",
		  (startpos-buf), (fillpos-buf), (endpos ? (endpos-buf) : -1));

	return result;
}

